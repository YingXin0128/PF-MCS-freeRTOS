#include "app.h"
#include "bsp.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

#define SENSOR_PERIOD_MS       100U
#define KEY_SCAN_PERIOD_MS      10U
#define DISPLAY_PERIOD_MS      100U
#define ALARM_PERIOD_MS        100U
#define CONTROL_PERIOD_MS      200U//修改控制任务的执行周期
#define LONG_PRESS_MS         2000U
#define DEBOUNCE_SAMPLES         3U
#define TOTAL_FLOW_MAX_L 999999999UL

typedef enum
{
    SCREEN_MAIN = 0,
    SCREEN_OUTPUT,
    SCREEN_PARAMETERS
} Screen;

typedef enum
{
    MODE_MANUAL = 0,
    MODE_AUTO
} WorkMode;

typedef enum
{
    KEY_EVENT_PRESS = 0,
    KEY_EVENT_RELEASE
} KeyEventType;

typedef struct
{
    BSP_Key key;
    KeyEventType type;
    uint32_t held_ms;
} KeyEvent;

typedef struct
{
    uint8_t stable_pressed;
    uint8_t candidate_pressed;
    uint8_t candidate_count;
    TickType_t pressed_at;
} KeyDebounce;

typedef struct
{
    Screen screen;
    WorkMode mode;

    uint16_t voltage_mv;
    uint16_t offset_mv;
    uint16_t pressure_tenths;
    uint32_t flow_hz;
    uint16_t flow_tenths;
    uint32_t total_flow_l;
    uint32_t total_flow_remainder;

    uint8_t duty_percent;
    uint8_t target_duty_percent;
    uint16_t target_pressure_tenths;
    uint16_t flow_high_tenths;
    uint16_t flow_low_tenths;
    uint16_t pressure_low_tenths;
    uint8_t duty_high_percent;

    uint8_t staged_target_duty;
    uint16_t staged_target_pressure;
    uint16_t staged_flow_high;
    uint16_t staged_flow_low;
    uint16_t staged_pressure_low;
    uint8_t staged_duty_high;
    uint8_t output_selection;
    uint8_t parameter_selection;

    uint8_t sensor_alarm;
    uint8_t blockage_alarm;
    uint8_t leakage_alarm;
    uint8_t adjusting;
} AppState;

static AppState s_state;
static SemaphoreHandle_t s_state_mutex;
static SemaphoreHandle_t s_bus_mutex;
static QueueHandle_t s_key_queue;


static void SensorTask(void *argument);
static void KeyTask(void *argument);
static void UiTask(void *argument);
static void ControlTask(void *argument);
static void AlarmTask(void *argument);
static void DisplayTask(void *argument);
static void State_Init(void);
static void Ui_HandleEvent(const KeyEvent *event);
static void Ui_HandlePageKey(void);
static void Ui_HandleFunctionKey(void);
static void Ui_AdjustSelected(int8_t direction);
static void Display_Render(const AppState *state);
static void Display_Line(uint8_t line, const char *text);
static uint16_t Pressure_FromVoltage(uint16_t voltage_mv, uint16_t offset_mv);

void APP_Start(void)
{
    BaseType_t result;

    /* 初始化应用层状态变量，比如当前模式、目标压力、PWM 占空比等 */
    State_Init();

    /* 创建状态互斥锁：
       多个任务都会读写 s_state，必须加锁，防止同时修改造成数据混乱 */
    s_state_mutex = xSemaphoreCreateMutex();

    /* 创建总线互斥锁：
       LCD、外设通信这类资源同一时间只能一个任务用 */
    s_bus_mutex = xSemaphoreCreateMutex();

    /* 创建按键消息队列：
       KeyTask 扫描到按键事件后，把事件放进这个队列；
       UiTask 从队列里取事件并处理 */
    s_key_queue = xQueueCreate(8U, sizeof(KeyEvent));

    /* 检查互斥锁和队列有没有创建成功。
       如果内存不够，创建失败，就进入错误处理 */
    if ((s_state_mutex == NULL) || (s_bus_mutex == NULL) || (s_key_queue == NULL))
    {
        Error_Handler();
    }

    /* 把队列和互斥锁注册到 FreeRTOS 调试列表里。
       这样在 Keil 的 RTOS 调试窗口里能看到名字，更方便观察 */
    vQueueAddToRegistry(s_key_queue, "KeyQueue");
    vQueueAddToRegistry(s_state_mutex, "StateLock");
    vQueueAddToRegistry(s_bus_mutex, "BusLock");

    /* 创建传感器任务：
       周期采集压力、电压、流量等数据
       优先级：tskIDLE_PRIORITY + 4，比较高 */
    result = xTaskCreate(SensorTask, "Sensor", 256U, NULL, tskIDLE_PRIORITY + 4U, NULL);

    /* 创建按键扫描任务：
       周期扫描 B1/B2/B3/B4，并把按键事件发送到队列 */
    if (result == pdPASS)
        result = xTaskCreate(KeyTask, "KeyScan", 256U, NULL, tskIDLE_PRIORITY + 4U, NULL);

    /* 创建 UI 任务：
       从按键队列里等待消息，处理页面切换、参数修改、确认提交 */
    if (result == pdPASS)
        result = xTaskCreate(UiTask, "UI", 320U, NULL, tskIDLE_PRIORITY + 3U, NULL);

    /* 创建控制任务：
       根据目标值慢慢调整 PWM，占空比不是瞬间跳变，而是周期性靠近目标 */
    if (result == pdPASS)
        result = xTaskCreate(ControlTask, "Control", 256U, NULL, tskIDLE_PRIORITY + 3U, NULL);

    /* 创建报警任务：
       判断压力、流量等是否超过阈值，控制 LED 或报警状态 */
    if (result == pdPASS)
        result = xTaskCreate(AlarmTask, "Alarm", 256U, NULL, tskIDLE_PRIORITY + 2U, NULL);

    /* 创建显示任务：
       周期刷新 LCD，把当前状态、参数、报警信息显示出来
       栈给得最大，因为显示函数通常局部变量和调用层级更多 */
    if (result == pdPASS)
        result = xTaskCreate(DisplayTask, "Display", 512U, NULL, tskIDLE_PRIORITY + 1U, NULL);

    /* 只要有任何一个任务创建失败，就进入错误处理 */
    if (result != pdPASS)
    {
        Error_Handler();
    }

    /* 启动 FreeRTOS 调度器。
       从这里开始，CPU 不再顺序执行 main 里的 while(1)，
       而是由 RTOS 按优先级和阻塞状态调度各个任务运行 */
    vTaskStartScheduler();

    /* 正常情况下不会运行到这里。
       如果运行到这里，通常说明调度器启动失败，比如堆内存不足 */
    Error_Handler();
}

static void State_Init(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_state.screen = SCREEN_MAIN;
    s_state.mode = MODE_MANUAL;
    s_state.duty_percent = 5U;
    s_state.target_duty_percent = 5U;
    s_state.target_pressure_tenths = 50U;
    s_state.flow_high_tenths = 200U;
    s_state.flow_low_tenths = 100U;
    s_state.pressure_low_tenths = 10U;
    s_state.duty_high_percent = 65U;
}

static void SensorTask(void *argument)
{
    TickType_t wake = xTaskGetTickCount();
    uint16_t voltage_mv;
    uint32_t flow_hz;
    uint16_t flow_tenths;

    (void)argument;
    for (;;)
    {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(SENSOR_PERIOD_MS));
        voltage_mv = BSP_Pressure_ReadMillivolts();
        flow_hz = BSP_Flow_ReadHertz();

        if (flow_hz < 800U)
        {
            flow_tenths = 0U;
        }
        else if (flow_hz > 8000U)
        {
            flow_tenths = 400U;
        }
        else
        {
            flow_tenths = (uint16_t)((flow_hz + 10U) / 20U);
        }

        xSemaphoreTake(s_state_mutex, portMAX_DELAY);
        s_state.voltage_mv = voltage_mv;
        s_state.pressure_tenths = Pressure_FromVoltage(voltage_mv, s_state.offset_mv);
        s_state.flow_hz = flow_hz;
        s_state.flow_tenths = flow_tenths;
        s_state.sensor_alarm = (uint8_t)(flow_hz > 8000U);

        if (s_state.total_flow_l < TOTAL_FLOW_MAX_L)
        {
            s_state.total_flow_remainder += flow_tenths;
            while (s_state.total_flow_remainder >= 6000U)
            {
                s_state.total_flow_remainder -= 6000U;
                ++s_state.total_flow_l;
                if (s_state.total_flow_l >= TOTAL_FLOW_MAX_L)
                {
                    s_state.total_flow_l = TOTAL_FLOW_MAX_L;
                    s_state.total_flow_remainder = 0U;
                    break;
                }
            }
        }
        xSemaphoreGive(s_state_mutex);
    }
}

static void KeyTask(void *argument)
{
    TickType_t wake = xTaskGetTickCount();
    KeyDebounce keys[BSP_KEY_COUNT];
    uint32_t index;

    (void)argument;
    memset(keys, 0, sizeof(keys));

    for (;;)
    {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(KEY_SCAN_PERIOD_MS));

        for (index = 0U; index < (uint32_t)BSP_KEY_COUNT; ++index)
        {
            uint8_t raw_pressed = BSP_Key_IsPressed((BSP_Key)index);
            KeyDebounce *key = &keys[index];

            if (raw_pressed == key->candidate_pressed)
            {
                if (key->candidate_count < DEBOUNCE_SAMPLES)
                {
                    ++key->candidate_count;
                }
            }
            else
            {
                key->candidate_pressed = raw_pressed;
                key->candidate_count = 1U;
            }

            if ((key->candidate_count >= DEBOUNCE_SAMPLES) &&
                (key->stable_pressed != key->candidate_pressed))
            {
                KeyEvent event;
                key->stable_pressed = key->candidate_pressed;
                event.key = (BSP_Key)index;

                if (key->stable_pressed != 0U)
                {
                    key->pressed_at = xTaskGetTickCount();
                    event.type = KEY_EVENT_PRESS;
                    event.held_ms = 0U;
                }
                else
                {
                    TickType_t held_ticks = xTaskGetTickCount() - key->pressed_at;
                    event.type = KEY_EVENT_RELEASE;
                    event.held_ms = (uint32_t)(held_ticks * portTICK_PERIOD_MS);
                }
                (void)xQueueSend(s_key_queue, &event, 0U);
            }
        }
    }
}

static void UiTask(void *argument)
{
    KeyEvent event;

    (void)argument;
    for (;;)
    {
        if (xQueueReceive(s_key_queue, &event, portMAX_DELAY) == pdPASS)
        {
            xSemaphoreTake(s_state_mutex, portMAX_DELAY);
            Ui_HandleEvent(&event);
            xSemaphoreGive(s_state_mutex);
        }
    }
}

static void ControlTask(void *argument)
{
    TickType_t wake = xTaskGetTickCount();

    (void)argument;
    for (;;)
    {
        uint8_t duty;
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(CONTROL_PERIOD_MS));//ControlTask 按固定周期醒来工作一次，其他时间处于阻塞状态，把 CPU 让给其他任务。

        xSemaphoreTake(s_state_mutex, portMAX_DELAY);
        duty = s_state.duty_percent;

        if (s_state.mode == MODE_MANUAL)
        {
            if (duty < s_state.target_duty_percent)
            {
                ++duty;
            }
            else if (duty > s_state.target_duty_percent)
            {
                --duty;
            }
            if (duty == s_state.target_duty_percent)
            {
                s_state.adjusting = 0U;
            }
        }
        else
        {
            if (s_state.pressure_tenths < (uint16_t)(s_state.target_pressure_tenths - 5U))
            {
                if (duty < 95U)
                {
                    ++duty;
                }
            }
            else if (s_state.pressure_tenths > (uint16_t)(s_state.target_pressure_tenths + 5U))
            {
                if (duty > 5U)
                {
                    --duty;
                }
            }
            else
            {
                s_state.adjusting = 0U;
            }
        }

        s_state.duty_percent = duty;
        xSemaphoreGive(s_state_mutex);
        BSP_PWM_SetDuty(duty);
    }
}

static void AlarmTask(void *argument)
{
    TickType_t wake = xTaskGetTickCount();
    uint16_t blockage_ms = 0U;
    uint16_t leakage_ms = 0U;

    (void)argument;
    for (;;)
    {
        uint8_t led_mask = 0U;
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(ALARM_PERIOD_MS));

        xSemaphoreTake(s_state_mutex, portMAX_DELAY);

        if ((s_state.flow_tenths < s_state.flow_low_tenths) &&
            (s_state.duty_percent > s_state.duty_high_percent))
        {
            if (blockage_ms <= 2000U)
            {
                blockage_ms = (uint16_t)(blockage_ms + ALARM_PERIOD_MS);
            }
            s_state.blockage_alarm = (uint8_t)(blockage_ms > 2000U);
        }
        else
        {
            blockage_ms = 0U;
            s_state.blockage_alarm = 0U;
        }

        if ((s_state.flow_tenths > s_state.flow_high_tenths) &&
            (s_state.pressure_tenths < s_state.pressure_low_tenths))
        {
            if (leakage_ms <= 2000U)
            {
                leakage_ms = (uint16_t)(leakage_ms + ALARM_PERIOD_MS);
            }
            s_state.leakage_alarm = (uint8_t)(leakage_ms > 2000U);
        }
        else
        {
            leakage_ms = 0U;
            s_state.leakage_alarm = 0U;
        }

        if (s_state.sensor_alarm != 0U)
        {
            led_mask |= 1U << 0;
        }
        if (s_state.blockage_alarm != 0U)
        {
            led_mask |= 1U << 1;
        }
        if (s_state.leakage_alarm != 0U)
        {
            led_mask |= 1U << 2;
        }
        if (s_state.adjusting != 0U)
        {
            led_mask |= 1U << 3;
        }
        xSemaphoreGive(s_state_mutex);

        xSemaphoreTake(s_bus_mutex, portMAX_DELAY);
        BSP_LED_Write(led_mask);
        xSemaphoreGive(s_bus_mutex);
    }
}

static void DisplayTask(void *argument)
{
    TickType_t wake = xTaskGetTickCount();
    AppState snapshot;

    (void)argument;
    for (;;)
    {
        xSemaphoreTake(s_state_mutex, portMAX_DELAY);
        memcpy(&snapshot, &s_state, sizeof(snapshot));
        xSemaphoreGive(s_state_mutex);

        xSemaphoreTake(s_bus_mutex, portMAX_DELAY);
        Display_Render(&snapshot);
        xSemaphoreGive(s_bus_mutex);

        vTaskDelayUntil(&wake, pdMS_TO_TICKS(DISPLAY_PERIOD_MS));
    }
}

static void Ui_HandleEvent(const KeyEvent *event)
{
    if (event->type == KEY_EVENT_PRESS)
    {
        switch (event->key)
        {
            case BSP_KEY_B1:
                Ui_HandlePageKey();
                break;
            case BSP_KEY_B2:
                Ui_HandleFunctionKey();
                break;
            case BSP_KEY_B3:
                if (s_state.screen != SCREEN_MAIN)
                {
                    Ui_AdjustSelected(1);
                }
                break;
            case BSP_KEY_B4:
                if (s_state.screen != SCREEN_MAIN)
                {
                    Ui_AdjustSelected(-1);
                }
                break;
            default:
                break;
        }
    }
    else if ((s_state.screen == SCREEN_MAIN) && (event->held_ms >= LONG_PRESS_MS))
    {
        if (event->key == BSP_KEY_B3)
        {
            s_state.offset_mv = s_state.voltage_mv;
            s_state.pressure_tenths = 0U;
        }
        else if (event->key == BSP_KEY_B4)
        {
            s_state.total_flow_l = 0U;
            s_state.total_flow_remainder = 0U;
        }
    }
}

static void Ui_HandlePageKey(void)
{
    if (s_state.screen == SCREEN_MAIN)
    {
        s_state.staged_target_duty = s_state.target_duty_percent;
        s_state.staged_target_pressure = s_state.target_pressure_tenths;
        s_state.output_selection = 0U;
        s_state.screen = SCREEN_OUTPUT;
    }
    else if (s_state.screen == SCREEN_OUTPUT)
    {
        uint8_t duty_changed = (uint8_t)(s_state.target_duty_percent != s_state.staged_target_duty);
        uint8_t pressure_changed = (uint8_t)(s_state.target_pressure_tenths != s_state.staged_target_pressure);

        if ((s_state.mode == MODE_MANUAL) && (duty_changed != 0U))
        {
            s_state.adjusting = (uint8_t)(s_state.duty_percent != s_state.staged_target_duty);
        }
        else if ((s_state.mode == MODE_AUTO) && (pressure_changed != 0U))
        {
            s_state.adjusting = (uint8_t)((s_state.pressure_tenths < (uint16_t)(s_state.staged_target_pressure - 5U)) ||
                                          (s_state.pressure_tenths > (uint16_t)(s_state.staged_target_pressure + 5U)));
        }
        s_state.target_duty_percent = s_state.staged_target_duty;
        s_state.target_pressure_tenths = s_state.staged_target_pressure;

        s_state.staged_flow_high = s_state.flow_high_tenths;
        s_state.staged_flow_low = s_state.flow_low_tenths;
        s_state.staged_pressure_low = s_state.pressure_low_tenths;
        s_state.staged_duty_high = s_state.duty_high_percent;
        s_state.parameter_selection = 0U;
        s_state.screen = SCREEN_PARAMETERS;
    }
    else
    {
        s_state.flow_high_tenths = s_state.staged_flow_high;
        s_state.flow_low_tenths = s_state.staged_flow_low;
        s_state.pressure_low_tenths = s_state.staged_pressure_low;
        s_state.duty_high_percent = s_state.staged_duty_high;
        s_state.screen = SCREEN_MAIN;
    }
}

static void Ui_HandleFunctionKey(void)
{
    if (s_state.screen == SCREEN_MAIN)
    {
        s_state.mode = (s_state.mode == MODE_MANUAL) ? MODE_AUTO : MODE_MANUAL;
    }
    else if (s_state.screen == SCREEN_OUTPUT)
    {
        s_state.output_selection = (uint8_t)((s_state.output_selection + 1U) % 2U);
    }
    else
    {
        s_state.parameter_selection = (uint8_t)((s_state.parameter_selection + 1U) % 4U);
    }
}

static void Ui_AdjustSelected(int8_t direction)
{
    if (s_state.screen == SCREEN_OUTPUT)
    {
        if (s_state.output_selection == 0U)
        {
            int16_t value = (int16_t)s_state.staged_target_duty + (int16_t)(direction * 5);
            if (value < 5)
            {
                value = 5;
            }
            else if (value > 95)
            {
                value = 95;
            }
            s_state.staged_target_duty = (uint8_t)value;
        }
        else
        {
            int16_t value = (int16_t)s_state.staged_target_pressure + (int16_t)(direction * 5);
            if (value < 10)
            {
                value = 10;
            }
            else if (value > 95)
            {
                value = 95;
            }
            s_state.staged_target_pressure = (uint16_t)value;
        }
    }
    else if (s_state.screen == SCREEN_PARAMETERS)
    {
        switch (s_state.parameter_selection)
        {
            case 0U:
            {
                int16_t value = (int16_t)s_state.staged_flow_high + (int16_t)(direction * 10);
                if (value < 40) value = 40;
                if (value > 400) value = 400;
                s_state.staged_flow_high = (uint16_t)value;
                break;
            }
            case 1U:
            {
                int16_t value = (int16_t)s_state.staged_flow_low + (int16_t)(direction * 10);
                if (value < 40) value = 40;
                if (value > 400) value = 400;
                s_state.staged_flow_low = (uint16_t)value;
                break;
            }
            case 2U:
            {
                int16_t value = (int16_t)s_state.staged_pressure_low + (int16_t)(direction * 5);
                if (value < 10) value = 10;
                if (value > 95) value = 95;
                s_state.staged_pressure_low = (uint16_t)value;
                break;
            }
            default:
            {
                int16_t value = (int16_t)s_state.staged_duty_high + (int16_t)(direction * 10);
                if (value < 5) value = 5;
                if (value > 95) value = 95;
                s_state.staged_duty_high = (uint8_t)value;
                break;
            }
        }
    }
}

static void Display_Render(const AppState *state)
{
    static Screen previous_screen = (Screen)0xFF;
    char text[21];

    if (state->screen != previous_screen)
    {
        LCD_Clear(Black);
        previous_screen = state->screen;
    }

    if (state->screen == SCREEN_MAIN)
    {
        Display_Line(Line1, "        MAIN        ");
        (void)snprintf(text, sizeof(text), "   M=%s", state->mode == MODE_AUTO ? "AUTO" : "MAN");
        Display_Line(Line3, text);
        (void)snprintf(text, sizeof(text), "   P=%u.%uBAR", state->pressure_tenths / 10U, state->pressure_tenths % 10U);
        Display_Line(Line4, text);
        (void)snprintf(text, sizeof(text), "   F=%u.%uL/M", state->flow_tenths / 10U, state->flow_tenths % 10U);
        Display_Line(Line5, text);
        (void)snprintf(text, sizeof(text), "   Q=%luL", (unsigned long)state->total_flow_l);
        Display_Line(Line6, text);
        (void)snprintf(text, sizeof(text), "   D=%u%%", state->duty_percent);
        Display_Line(Line7, text);
        (void)snprintf(text, sizeof(text), "   V=%u.%uV", state->offset_mv / 1000U, (state->offset_mv % 1000U) / 100U);
        Display_Line(Line8, text);
    }
    else if (state->screen == SCREEN_OUTPUT)
    {
        Display_Line(Line1, "        OUTP        ");
        (void)snprintf(text, sizeof(text), "   TarD=%u%%", state->staged_target_duty);
        Display_Line(Line3, text);
        (void)snprintf(text, sizeof(text), "   TarP=%u.%uBAR", state->staged_target_pressure / 10U, state->staged_target_pressure % 10U);
        Display_Line(Line4, text);
    }
    else
    {
        Display_Line(Line1, "        PARA        ");
        (void)snprintf(text, sizeof(text), "   FH=%u.%uL/M", state->staged_flow_high / 10U, state->staged_flow_high % 10U);
        Display_Line(Line3, text);
        (void)snprintf(text, sizeof(text), "   FL=%u.%uL/M", state->staged_flow_low / 10U, state->staged_flow_low % 10U);
        Display_Line(Line4, text);
        (void)snprintf(text, sizeof(text), "   PL=%u.%uBAR", state->staged_pressure_low / 10U, state->staged_pressure_low % 10U);
        Display_Line(Line5, text);
        (void)snprintf(text, sizeof(text), "   DH=%u%%", state->staged_duty_high);
        Display_Line(Line6, text);
    }
}

static void Display_Line(uint8_t line, const char *text)
{
    uint8_t padded[21];
    size_t length = strlen(text);

    if (length > 20U)
    {
        length = 20U;
    }
    memset(padded, ' ', 20U);
    memcpy(padded, text, length);
    padded[20] = '\0';
    LCD_DisplayStringLine(line, padded);
}

static uint16_t Pressure_FromVoltage(uint16_t voltage_mv, uint16_t offset_mv)
{
    uint32_t numerator;
    uint32_t denominator;
    uint32_t pressure;

    if ((voltage_mv <= offset_mv) || (offset_mv >= 3300U))
    {
        return 0U;
    }

    numerator = (uint32_t)(voltage_mv - offset_mv) * 100U;
    denominator = (uint32_t)(3300U - offset_mv);
    pressure = (numerator + denominator / 2U) / denominator;
    if (pressure > 100U)
    {
        pressure = 100U;
    }
    return (uint16_t)pressure;
}

void vApplicationMallocFailedHook(void)
{
    Error_Handler();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name;
    Error_Handler();
}

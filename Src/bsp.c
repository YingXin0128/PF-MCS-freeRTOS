#include "bsp.h"


#define PWM_PERIOD_COUNTS 8000U

static ADC_HandleTypeDef s_adc2;
static TIM_HandleTypeDef s_tim2;
static TIM_HandleTypeDef s_tim3;
static uint16_t s_flow_last_count;

static void BSP_GPIO_Init(void);
static void BSP_ADC2_Init(void);
static void BSP_TIM2_PWM_Init(void);
static void BSP_TIM3_FlowCounter_Init(void);

void BSP_Init(void)
{
    BSP_GPIO_Init();
    BSP_ADC2_Init();
    BSP_TIM2_PWM_Init();
    BSP_TIM3_FlowCounter_Init();
}

void BSP_Start(void)
{
    BSP_PWM_SetDuty(5U);
    if (HAL_TIM_PWM_Start(&s_tim2, TIM_CHANNEL_2) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_TIM_IC_Start(&s_tim3, TIM_CHANNEL_1) != HAL_OK)
    {
        Error_Handler();
    }
    s_flow_last_count = (uint16_t)__HAL_TIM_GET_COUNTER(&s_tim3);
}

uint16_t BSP_Pressure_ReadMillivolts(void)
{
    uint32_t sum = 0U;
    uint32_t i;

    for (i = 0U; i < 8U; ++i)
    {
        if (HAL_ADC_Start(&s_adc2) != HAL_OK)
        {
            Error_Handler();
        }
        if (HAL_ADC_PollForConversion(&s_adc2, 2U) != HAL_OK)
        {
            Error_Handler();
        }
        sum += HAL_ADC_GetValue(&s_adc2);
        (void)HAL_ADC_Stop(&s_adc2);
    }

    return (uint16_t)((sum * 3300U + (4095U * 4U)) / (4095U * 8U));
}

uint32_t BSP_Flow_ReadHertz(void)
{
    uint16_t now = (uint16_t)__HAL_TIM_GET_COUNTER(&s_tim3);
    uint16_t pulses = (uint16_t)(now - s_flow_last_count);

    s_flow_last_count = now;
    return (uint32_t)pulses * 10U;
}

uint8_t BSP_Key_IsPressed(BSP_Key key)
{
    GPIO_PinState level = GPIO_PIN_SET;

    switch (key)
    {
        case BSP_KEY_B1:
            level = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_0);
            break;
        case BSP_KEY_B2:
            level = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_1);
            break;
        case BSP_KEY_B3:
            level = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_2);
            break;
        case BSP_KEY_B4:
            level = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0);
            break;
        default:
            break;
    }

    return (uint8_t)(level == GPIO_PIN_RESET);
}

void BSP_PWM_SetDuty(uint8_t duty_percent)
{
    uint32_t compare;

    if (duty_percent < 5U)
    {
        duty_percent = 5U;
    }
    else if (duty_percent > 95U)
    {
        duty_percent = 95U;
    }

    compare = ((uint32_t)duty_percent * PWM_PERIOD_COUNTS) / 100U;
    __HAL_TIM_SET_COMPARE(&s_tim2, TIM_CHANNEL_2, compare);
}

void BSP_LED_Write(uint8_t on_mask)
{
    uint32_t saved_odr = GPIOC->ODR;
    uint8_t active_low_data = (uint8_t)~on_mask;

    GPIOC->ODR = (saved_odr & 0x00FFU) | ((uint32_t)active_low_data << 8U);
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_2, GPIO_PIN_SET);
    __NOP();
    __NOP();
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_2, GPIO_PIN_RESET);
    GPIOC->ODR = saved_odr;
}

static void BSP_GPIO_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_0;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &gpio);

    gpio.Pin = GPIO_PIN_15;
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &gpio);

    gpio.Pin = GPIO_PIN_1;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF1_TIM2;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = GPIO_PIN_4;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF2_TIM3;
    HAL_GPIO_Init(GPIOB, &gpio);

    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_2, GPIO_PIN_RESET);
    gpio.Pin = GPIO_PIN_2;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOD, &gpio);
}

static void BSP_ADC2_Init(void)
{
    ADC_ChannelConfTypeDef channel = {0};

    __HAL_RCC_ADC12_CLK_ENABLE();

    s_adc2.Instance = ADC2;
    s_adc2.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    s_adc2.Init.Resolution = ADC_RESOLUTION_12B;
    s_adc2.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    s_adc2.Init.GainCompensation = 0U;
    s_adc2.Init.ScanConvMode = ADC_SCAN_DISABLE;
    s_adc2.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    s_adc2.Init.LowPowerAutoWait = DISABLE;
    s_adc2.Init.ContinuousConvMode = DISABLE;
    s_adc2.Init.NbrOfConversion = 1U;
    s_adc2.Init.DiscontinuousConvMode = DISABLE;
    s_adc2.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    s_adc2.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    s_adc2.Init.DMAContinuousRequests = DISABLE;
    s_adc2.Init.Overrun = ADC_OVR_DATA_PRESERVED;
    s_adc2.Init.OversamplingMode = DISABLE;
    if (HAL_ADC_Init(&s_adc2) != HAL_OK)
    {
        Error_Handler();
    }

    channel.Channel = ADC_CHANNEL_15;
    channel.Rank = ADC_REGULAR_RANK_1;
    channel.SamplingTime = ADC_SAMPLETIME_247CYCLES_5;
    channel.SingleDiff = ADC_SINGLE_ENDED;
    channel.OffsetNumber = ADC_OFFSET_NONE;
    channel.Offset = 0U;
    if (HAL_ADC_ConfigChannel(&s_adc2, &channel) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_ADCEx_Calibration_Start(&s_adc2, ADC_SINGLE_ENDED) != HAL_OK)
    {
        Error_Handler();
    }
}

static void BSP_TIM2_PWM_Init(void)
{
    TIM_ClockConfigTypeDef clock_source = {0};
    TIM_OC_InitTypeDef pwm = {0};

    __HAL_RCC_TIM2_CLK_ENABLE();

    s_tim2.Instance = TIM2;
    s_tim2.Init.Prescaler = 0U;
    s_tim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    s_tim2.Init.Period = PWM_PERIOD_COUNTS - 1U;
    s_tim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    s_tim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_PWM_Init(&s_tim2) != HAL_OK)
    {
        Error_Handler();
    }

    clock_source.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
    if (HAL_TIM_ConfigClockSource(&s_tim2, &clock_source) != HAL_OK)
    {
        Error_Handler();
    }

    pwm.OCMode = TIM_OCMODE_PWM1;
    pwm.Pulse = PWM_PERIOD_COUNTS / 20U;
    pwm.OCPolarity = TIM_OCPOLARITY_HIGH;
    pwm.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&s_tim2, &pwm, TIM_CHANNEL_2) != HAL_OK)
    {
        Error_Handler();
    }
}

static void BSP_TIM3_FlowCounter_Init(void)
{
    TIM_SlaveConfigTypeDef slave = {0};
    TIM_IC_InitTypeDef input = {0};

    __HAL_RCC_TIM3_CLK_ENABLE();

    s_tim3.Instance = TIM3;
    s_tim3.Init.Prescaler = 0U;
    s_tim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    s_tim3.Init.Period = 0xFFFFU;
    s_tim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    s_tim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_IC_Init(&s_tim3) != HAL_OK)
    {
        Error_Handler();
    }

    input.ICPolarity = TIM_INPUTCHANNELPOLARITY_RISING;
    input.ICSelection = TIM_ICSELECTION_DIRECTTI;
    input.ICPrescaler = TIM_ICPSC_DIV1;
    input.ICFilter = 2U;
    if (HAL_TIM_IC_ConfigChannel(&s_tim3, &input, TIM_CHANNEL_1) != HAL_OK)
    {
        Error_Handler();
    }

    slave.SlaveMode = TIM_SLAVEMODE_EXTERNAL1;
    slave.InputTrigger = TIM_TS_TI1FP1;
    slave.TriggerPolarity = TIM_TRIGGERPOLARITY_RISING;
    slave.TriggerPrescaler = TIM_TRIGGERPRESCALER_DIV1;
    slave.TriggerFilter = 2U;
    if (HAL_TIM_SlaveConfigSynchro(&s_tim3, &slave) != HAL_OK)
    {
        Error_Handler();
    }
}


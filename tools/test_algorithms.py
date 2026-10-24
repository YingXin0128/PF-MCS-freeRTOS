"""Run the hardware application's actual C logic on a host with test-only stubs.

No virtual firmware target is created. The stubs only exist in a temporary folder.
Usage: python tools/test_algorithms.py --cc /path/to/clang
       python tools/test_algorithms.py --cc /path/to/zig --report docs/data/software-tests.json
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

STUB = r'''
#ifndef PF_TEST_STUB_H
#define PF_TEST_STUB_H
#include <stdint.h>
#include <stdlib.h>
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef void *SemaphoreHandle_t;
typedef void *QueueHandle_t;
typedef void *TaskHandle_t;
typedef enum { BSP_KEY_B1, BSP_KEY_B2, BSP_KEY_B3, BSP_KEY_B4, BSP_KEY_COUNT } BSP_Key;
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(x) (x)
#define portTICK_PERIOD_MS 1U
#define tskIDLE_PRIORITY 0U
#define Black 0
#define Line1 0
#define Line3 32
#define Line4 48
#define Line5 64
#define Line6 80
#define Line7 96
#define Line8 112
static TickType_t fake_tick;
static unsigned remaining, iteration;
static uint16_t fake_mv;
static uint32_t fake_hz;
static uint8_t fake_keys[BSP_KEY_COUNT], last_duty, last_led;
static void (*on_tick)(void);
void Error_Handler(void);
void vTaskDelayUntil(TickType_t *, TickType_t);
static TickType_t xTaskGetTickCount(void) { return fake_tick; }
static SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
static QueueHandle_t xQueueCreate(unsigned n, unsigned s) { (void)n; (void)s; return (void *)1; }
static int xSemaphoreTake(SemaphoreHandle_t h, TickType_t t) { (void)h; (void)t; return pdPASS; }
static int xSemaphoreGive(SemaphoreHandle_t h) { (void)h; return pdPASS; }
static void vQueueAddToRegistry(void *h, const char *n) { (void)h; (void)n; }
static int xTaskCreate(void (*f)(void *), const char *n, unsigned s, void *a, unsigned p, void *h)
{ (void)f; (void)n; (void)s; (void)a; (void)p; (void)h; return pdPASS; }
static void vTaskStartScheduler(void) {}
static int xQueueReceive(void *q, void *e, TickType_t t) { (void)q; (void)e; (void)t; return 0; }
int xQueueSend(void *, const void *, TickType_t);
static uint16_t BSP_Pressure_ReadMillivolts(void) { return fake_mv; }
static uint32_t BSP_Flow_ReadHertz(void) { return fake_hz; }
static uint8_t BSP_Key_IsPressed(BSP_Key k) { return fake_keys[k]; }
static void BSP_PWM_SetDuty(uint8_t d) { last_duty = d; }
static void BSP_LED_Write(uint8_t m) { last_led = m; }
static void LCD_Clear(int c) { (void)c; }
static void LCD_DisplayStringLine(uint8_t l, uint8_t *t) { (void)l; (void)t; }
#endif
'''

HARNESS = r'''
#include <assert.h>
#include <setjmp.h>
#include "test_stub.h"
#include "APP_SOURCE"
static jmp_buf task_exit;
static unsigned checks, groups, event_count;
static KeyEvent events[16];
static uint8_t alarm_history[64];
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } ++checks; } while (0)
void Error_Handler(void) { abort(); }
int xQueueSend(void *q, const void *e, TickType_t t) {
    (void)q; (void)t;
    if (event_count >= 16) abort();
    events[event_count++] = *(const KeyEvent *)e;
    return pdPASS;
}
void vTaskDelayUntil(TickType_t *wake, TickType_t period) {
    if (!remaining--) longjmp(task_exit, 1);
    *wake += period; fake_tick = *wake; ++iteration;
    if (on_tick) on_tick();
}
static void run_task(void (*task)(void *), unsigned ticks) {
    remaining = ticks; iteration = 0;
    if (!setjmp(task_exit)) task(NULL);
}
static void reset(void) {
    State_Init(); fake_tick = 0; fake_mv = 0; fake_hz = 0;
    last_duty = 0; last_led = 0; on_tick = NULL; event_count = 0;
    memset(fake_keys, 0, sizeof(fake_keys));
}
static void key(BSP_Key k, KeyEventType type, uint32_t held) {
    KeyEvent e = {k, type, held}; Ui_HandleEvent(&e);
}
static void pressure_tests(void) {
    const unsigned vectors[][3] = {
        {0,0,0}, {330,0,10}, {825,0,25}, {1650,0,50},
        {2475,0,75}, {3300,0,100}, {4000,0,100},
        {300,300,0}, {200,300,0}, {1800,300,50},
        {3300,300,100}, {3300,3300,0}, {3300,4000,0}
    };
    for (unsigned i = 0; i < sizeof(vectors)/sizeof(vectors[0]); ++i)
        CHECK(Pressure_FromVoltage(vectors[i][0], vectors[i][1]) == vectors[i][2]);
    ++groups;
}
static void flow_tests(void) {
    const unsigned vectors[][3] = {
        {0,0,0}, {790,0,0}, {800,40,0}, {1000,50,0},
        {4000,200,0}, {8000,400,0}, {8010,400,1}, {12000,400,1}
    };
    for (unsigned i = 0; i < sizeof(vectors)/sizeof(vectors[0]); ++i) {
        reset(); fake_hz = vectors[i][0]; run_task(SensorTask,1);
        CHECK(s_state.flow_tenths == vectors[i][1]);
        CHECK(s_state.sensor_alarm == vectors[i][2]);
    }
    reset(); fake_hz = 4000; run_task(SensorTask,29);
    CHECK(s_state.total_flow_l == 0 && s_state.total_flow_remainder == 5800);
    run_task(SensorTask,1); CHECK(s_state.total_flow_l == 1 && s_state.total_flow_remainder == 0);
    run_task(SensorTask,570); CHECK(s_state.total_flow_l == 20);
    reset(); fake_hz = 8000; run_task(SensorTask,600); CHECK(s_state.total_flow_l == 40);
    s_state.total_flow_l = TOTAL_FLOW_MAX_L - 1; s_state.total_flow_remainder = 5800;
    run_task(SensorTask,1); CHECK(s_state.total_flow_l == TOTAL_FLOW_MAX_L && s_state.total_flow_remainder == 0);
    run_task(SensorTask,100); CHECK(s_state.total_flow_l == TOTAL_FLOW_MAX_L);
    ++groups;
}
static void control_tests(void) {
    reset(); s_state.target_duty_percent = 65; s_state.adjusting = 1;
    run_task(ControlTask,59); CHECK(last_duty == 64 && s_state.adjusting == 1);
    run_task(ControlTask,1); CHECK(last_duty == 65 && s_state.adjusting == 0);
    CHECK(fake_tick == 12000);
    run_task(ControlTask,5); CHECK(last_duty == 65);
    s_state.target_duty_percent = 5; run_task(ControlTask,60); CHECK(last_duty == 5);
    const unsigned pressures[] = {44,45,50,55,56};
    const unsigned expected[] = {51,50,50,50,49};
    for (unsigned i=0;i<5;++i) {
        reset(); s_state.mode = MODE_AUTO; s_state.duty_percent = 50;
        s_state.pressure_tenths = pressures[i]; run_task(ControlTask,1);
        CHECK(last_duty == expected[i]);
    }
    reset(); s_state.mode = MODE_AUTO; s_state.pressure_tenths = 0;
    s_state.duty_percent = 95; run_task(ControlTask,10); CHECK(last_duty == 95);
    s_state.pressure_tenths = 100; s_state.duty_percent = 5;
    run_task(ControlTask,10); CHECK(last_duty == 5);
    ++groups;
}
static void record_alarm(void) {
    if (iteration > 1) alarm_history[iteration-2] = last_led;
    if (iteration == 23) s_state.flow_tenths = 200;
}
static void alarm_tests(void) {
    reset(); s_state.flow_tenths = 50; s_state.duty_percent = 70;
    on_tick = record_alarm; run_task(AlarmTask,23);
    CHECK(alarm_history[19] == 0); CHECK(alarm_history[20] == 2);
    CHECK(last_led == 0); CHECK(s_state.blockage_alarm == 0);
    reset(); s_state.flow_tenths = 250; s_state.pressure_tenths = 5;
    run_task(AlarmTask,20); CHECK(last_led == 0);
    /* Counters are local to AlarmTask: use a fresh 21-sample invocation. */
    run_task(AlarmTask,21); CHECK(last_led == 4);
    reset(); s_state.flow_tenths = 100; s_state.duty_percent = 70;
    run_task(AlarmTask,21); CHECK(last_led == 0);
    s_state.flow_tenths = 50; s_state.duty_percent = 65;
    run_task(AlarmTask,21); CHECK(last_led == 0);
    s_state.sensor_alarm = 1; s_state.adjusting = 1;
    run_task(AlarmTask,1); CHECK(last_led == 9); CHECK((last_led & 0xf0) == 0);
    ++groups;
}
static void ui_tests(void) {
    reset(); key(BSP_KEY_B1,KEY_EVENT_PRESS,0);
    CHECK(s_state.screen == SCREEN_OUTPUT);
    key(BSP_KEY_B3,KEY_EVENT_PRESS,0);
    CHECK(s_state.staged_target_duty == 10 && s_state.target_duty_percent == 5);
    key(BSP_KEY_B1,KEY_EVENT_PRESS,0);
    CHECK(s_state.screen == SCREEN_PARAMETERS && s_state.target_duty_percent == 10);
    CHECK(s_state.adjusting == 1);
    key(BSP_KEY_B3,KEY_EVENT_PRESS,0);
    CHECK(s_state.staged_flow_high == 210 && s_state.flow_high_tenths == 200);
    key(BSP_KEY_B1,KEY_EVENT_PRESS,0);
    CHECK(s_state.screen == SCREEN_MAIN && s_state.flow_high_tenths == 210);
    key(BSP_KEY_B2,KEY_EVENT_PRESS,0); CHECK(s_state.mode == MODE_AUTO);
    key(BSP_KEY_B2,KEY_EVENT_PRESS,0); CHECK(s_state.mode == MODE_MANUAL);
    s_state.voltage_mv = 300; s_state.pressure_tenths = 10;
    key(BSP_KEY_B3,KEY_EVENT_RELEASE,1999); CHECK(s_state.offset_mv == 0);
    key(BSP_KEY_B3,KEY_EVENT_RELEASE,2000);
    CHECK(s_state.offset_mv == 300 && s_state.pressure_tenths == 0);
    s_state.total_flow_l = 42; s_state.total_flow_remainder = 123;
    key(BSP_KEY_B4,KEY_EVENT_RELEASE,2000);
    CHECK(s_state.total_flow_l == 0 && s_state.total_flow_remainder == 0);
    key(BSP_KEY_B1,KEY_EVENT_PRESS,0);
    for (unsigned i=0;i<30;++i) key(BSP_KEY_B3,KEY_EVENT_PRESS,0);
    CHECK(s_state.staged_target_duty == 95);
    for (unsigned i=0;i<30;++i) key(BSP_KEY_B4,KEY_EVENT_PRESS,0);
    CHECK(s_state.staged_target_duty == 5);
    ++groups;
}
static void bounce_input(void) {
    fake_keys[0] = (iteration == 1 || iteration == 3 ||
                   (iteration >= 5 && iteration <= 7));
}
static void long_input(void) { fake_keys[2] = (iteration <= 203); }
static void key_tests(void) {
    reset(); on_tick = bounce_input; run_task(KeyTask,10);
    CHECK(event_count == 2); CHECK(events[0].type == KEY_EVENT_PRESS);
    CHECK(events[1].type == KEY_EVENT_RELEASE); CHECK(events[1].held_ms == 30);
    reset(); on_tick = long_input; run_task(KeyTask,206);
    CHECK(event_count == 2); CHECK(events[1].key == BSP_KEY_B3);
    CHECK(events[1].held_ms == 2030);
    ++groups;
}
int main(void) {
    pressure_tests(); flow_tests(); control_tests(); alarm_tests(); ui_tests(); key_tests();
    printf("{\"groups\":%u,\"assertions\":%u,\"passed\":true,\"source\":\"Src/app.c\","
           "\"execution\":\"host C with temporary test stubs\",\"hardware_measured\":false}\n", groups,checks);
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', help='Host C compiler path (Clang, GCC or Zig)')
    parser.add_argument('--report', type=Path, help='Optional JSON result, separate from raw logs')
    args = parser.parse_args()
    compiler = args.cc or shutil.which('clang') or shutil.which('gcc') or shutil.which('cc')
    if not compiler:
        parser.error('Provide --cc with a host C compiler path.')
    with tempfile.TemporaryDirectory(prefix='pf-mcs-test-') as temp:
        folder = Path(temp)
        (folder / 'test_stub.h').write_text(STUB, encoding='utf-8')
        for name in ('app.h', 'bsp.h', 'FreeRTOS.h', 'queue.h', 'semphr.h', 'task.h'):
            (folder / name).write_text('#include "test_stub.h"\n', encoding='utf-8')
        source = HARNESS.replace('APP_SOURCE', (ROOT / 'Src/app.c').as_posix())
        (folder / 'test.c').write_text(source, encoding='utf-8')
        executable = folder / ('test.exe' if __import__('os').name == 'nt' else 'test')
        command = [compiler]
        if Path(compiler).stem.lower() == 'zig':
            command.append('cc')
        command += ['-std=c99', '-O0', '-Wall', '-Wextra', '-Werror', '-I', str(folder),
                    str(folder / 'test.c'), '-o', str(executable)]
        subprocess.run(command, check=True)
        result = json.loads(subprocess.check_output([str(executable)], text=True))
    result['source_sha256'] = hashlib.sha256((ROOT / 'Src/app.c').read_bytes()).hexdigest()
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(result, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    print(json.dumps(result, ensure_ascii=False))


if __name__ == '__main__':
    main()

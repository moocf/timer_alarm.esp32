#include <stdio.h>
#include <esp_types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <driver/periph_ctrl.h>
#include <driver/timer.h>
#include "macros.h"


#define TIMER_DIVIDER         16
#define TIMER_SCALE           (TIMER_BASE_CLK / TIMER_DIVIDER)
#define TIMER_INTERVAL0_SEC   (3.4179)
#define TIMER_INTERVAL1_SEC   (5.78)
#define TEST_WITHOUT_RELOAD   0
#define TEST_WITH_RELOAD      1


typedef struct {
  int type;
  int group;
  int id;
  uint64_t count;
} timer_event_t;


xQueueHandle timer_queue;


static void inline print_timer_counter(uint64_t counter_value) {
  printf("Counter: 0x%08x%08x\n", (uint32_t) (counter_value >> 32),
                                  (uint32_t) (counter_value));
  printf("Time   : %.8f s\n", (double) counter_value / TIMER_SCALE);
}


void IRAM_ATTR on_timer(void *arg) {
  int id = (int) arg;
  uint32_t status = TIMERG0.int_st_timers.val;
  TIMERG0.hw_timer[id].update = 1;
  uint64_t count =  ((uint64_t) TIMERG0.hw_timer[id].cnt_high) << 32 | TIMERG0.hw_timer[id].cnt_low;

  timer_event_t e = {
    .group = 0,
    .id = id,
    .count = count,
  };

  if ((status & BIT(id)) && id == TIMER_0) {
    e.type = TEST_WITHOUT_RELOAD;
    TIMERG0.int_clr_timers.t0 = 1;
    count += (uint64_t) (TIMER_INTERVAL0_SEC * TIMER_SCALE);
    TIMERG0.hw_timer[id].alarm_high = (uint32_t) (count >> 32);
    TIMERG0.hw_timer[id].alarm_low = (uint32_t) count;
  } else if ((status & BIT(id)) && id == TIMER_1) {
    e.type = TEST_WITH_RELOAD;
    TIMERG0.int_clr_timers.t1 = 1;
  } else {
    e.type = -1;
  }

  TIMERG0.hw_timer[id].config.alarm_en = TIMER_ALARM_EN;
  xQueueSendFromISR(timer_queue, &e, NULL);
}


static void timer_begin(int id, bool auto_reload, double interval) {
  timer_config_t c = {
    .divider = TIMER_DIVIDER,
    .counter_dir = TIMER_COUNT_UP,
    .counter_en = TIMER_PAUSE,
    .alarm_en = TIMER_ALARM_EN,
    .intr_type = TIMER_INTR_LEVEL,
    .auto_reload = auto_reload,
  };
  timer_init(TIMER_GROUP_0, id, &c);
  timer_set_counter_value(TIMER_GROUP_0, id, 0x00000000ULL);
  timer_set_alarm_value(TIMER_GROUP_0, id, interval * TIMER_SCALE);
  timer_enable_intr(TIMER_GROUP_0, id);
  timer_isr_register(TIMER_GROUP_0, id, on_timer, (void*) id, ESP_INTR_FLAG_IRAM, NULL);
  timer_start(TIMER_GROUP_0, id);
}


static void task_timer_evt(void *arg) {
  while (true) {
    timer_event_t e;
    xQueueReceive(timer_queue, &e, portMAX_DELAY);
    if (e.type == TEST_WITHOUT_RELOAD) {
      printf("\n    Example timer without reload\n");
    } else if (e.type == TEST_WITH_RELOAD) {
      printf("\n    Example timer with auto reload\n");
    } else {
      printf("\n    UNKNOWN EVENT TYPE\n");
    }
    printf("Group[%d], timer[%d] alarm event\n", e.group, e.id);
    printf("------- EVENT TIME --------\n");
    print_timer_counter(e.count);
    printf("-------- TASK TIME --------\n");
    uint64_t task_counter_value;
    timer_get_counter_value(e.group, e.id, &task_counter_value);
    print_timer_counter(task_counter_value);
  }
}


void app_main() {
  timer_queue = xQueueCreate(10, sizeof(timer_event_t));
  timer_begin(TIMER_0, TEST_WITHOUT_RELOAD, TIMER_INTERVAL0_SEC);
  timer_begin(TIMER_1, TEST_WITH_RELOAD,    TIMER_INTERVAL1_SEC);
  xTaskCreate(task_timer_evt, "timer_evt", 2048, NULL, 5, NULL);
}

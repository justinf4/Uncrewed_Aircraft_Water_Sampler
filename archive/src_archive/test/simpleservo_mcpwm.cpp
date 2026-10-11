#include <Arduino.h>
#include "driver/mcpwm_prelude.h"

// -------- MCPWM HANDLES --------
mcpwm_timer_handle_t timer[2];
mcpwm_oper_handle_t  oper[4];
mcpwm_cmpr_handle_t  cmpr[4];
mcpwm_gen_handle_t   gen[4];

// -------- PINS --------
const int SERVO_PINS[4] = {D9, D7, D5, D2};

// -------- STATE --------
int mode = 0;
unsigned long prevMillis = 0;

// -------- TIMING --------
const uint32_t STOP  = 1510;
const uint32_t OPEN  = 1000;
const uint32_t CLOSE = 2000;

const unsigned long CLOSE_TIME = 2600;
const unsigned long OPEN_TIME  = 2600;

// -------- WRITE --------
void servo_write_us(int idx, uint32_t us) {
  mcpwm_comparator_set_compare_value(cmpr[idx], us);
}

// -------- INIT --------
void servo_init() {
  // --- Timers: one per MCPWM group ---
  mcpwm_timer_config_t timer_cfg = {};
  timer_cfg.resolution_hz = 1000000; // 1MHz → 1us per tick
  timer_cfg.count_mode    = MCPWM_TIMER_COUNT_MODE_UP;
  timer_cfg.period_ticks  = 20000;   // 20ms period = 50Hz

  timer_cfg.group_id = 0;
  ESP_ERROR_CHECK(mcpwm_new_timer(&timer_cfg, &timer[0]));
  timer_cfg.group_id = 1;
  ESP_ERROR_CHECK(mcpwm_new_timer(&timer_cfg, &timer[1]));

  // --- Operators, comparators, generators: one per servo ---
  for (int i = 0; i < 4; i++) {
    // servos 0,1 → group 0 | servos 2,3 → group 1
    mcpwm_operator_config_t oper_cfg = {};
    oper_cfg.group_id = i / 2;
    ESP_ERROR_CHECK(mcpwm_new_operator(&oper_cfg, &oper[i]));
    ESP_ERROR_CHECK(mcpwm_operator_connect_timer(oper[i], timer[i / 2]));

    mcpwm_comparator_config_t cmpr_cfg = {};
    cmpr_cfg.flags.update_cmp_on_tez = true; // update on timer == zero (clean, no glitch)
    ESP_ERROR_CHECK(mcpwm_new_comparator(oper[i], &cmpr_cfg, &cmpr[i]));

    mcpwm_generator_config_t gen_cfg = {};
    gen_cfg.gen_gpio_num = SERVO_PINS[i];
    ESP_ERROR_CHECK(mcpwm_new_generator(oper[i], &gen_cfg, &gen[i]));

    // Pin goes HIGH at start of period, LOW when comparator matches
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(gen[i],
      MCPWM_GEN_TIMER_EVENT_ACTION(
        MCPWM_TIMER_DIRECTION_UP,
        MCPWM_TIMER_EVENT_EMPTY,
        MCPWM_GEN_ACTION_HIGH)));

    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_compare_event(gen[i],
      MCPWM_GEN_COMPARE_EVENT_ACTION(
        MCPWM_TIMER_DIRECTION_UP,
        cmpr[i],
        MCPWM_GEN_ACTION_LOW)));

    // Set initial pulse to STOP
    ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(cmpr[i], STOP));
  }

  // --- Start both timers ---
  ESP_ERROR_CHECK(mcpwm_timer_enable(timer[0]));
  ESP_ERROR_CHECK(mcpwm_timer_start_stop(timer[0], MCPWM_TIMER_START_NO_STOP));
  ESP_ERROR_CHECK(mcpwm_timer_enable(timer[1]));
  ESP_ERROR_CHECK(mcpwm_timer_start_stop(timer[1], MCPWM_TIMER_START_NO_STOP));
}

// -------- SETUP --------
void setup() {
  Serial.begin(115200);
  servo_init();
}

// -------- LOOP --------
void loop() {
  switch (mode) {

    case 0:  // OPEN s1
      servo_write_us(0, OPEN);
      if (millis() - prevMillis >= OPEN_TIME) {
        prevMillis = millis();
        mode = 1;
      }
      break;

    case 1:  // STOP s1
      servo_write_us(0, STOP);
      if (millis() - prevMillis >= CLOSE_TIME) {
        prevMillis = millis();
        mode = 0;
      }
      break;
  }
}
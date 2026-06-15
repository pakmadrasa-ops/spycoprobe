#ifndef __SBW_HAL_GLUE_H_
#define __SBW_HAL_GLUE_H_

/* These definitions glue the platform's HAL function to the SBW code */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"

typedef enum { GPIO_DIR_OUT, GPIO_DIR_IN } gpio_dir_t;

typedef enum { GPIO_STATE_LOW, GPIO_STATE_HIGH } gpio_state_t;

extern portMUX_TYPE sbw_hal_mux;

static inline int hal_gpio_init(unsigned int pin) {
  gpio_reset_pin((gpio_num_t)pin);
  return 0;
}

static inline int hal_gpio_dir(unsigned int pin, gpio_dir_t dir) {
  if (dir == GPIO_DIR_IN) {
    gpio_set_direction((gpio_num_t)pin, GPIO_MODE_INPUT);
  } else {
    gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
  }
  return 0;
};

static inline int hal_gpio_cfg(unsigned int pin, gpio_dir_t dir,
                               gpio_state_t state) {
  gpio_set_level((gpio_num_t)pin, (state == GPIO_STATE_HIGH) ? 1 : 0);
  hal_gpio_dir(pin, dir);
  return 0;
}

static inline int hal_gpio_set(unsigned int pin, gpio_state_t state) {
  gpio_set_level((gpio_num_t)pin, (state == GPIO_STATE_HIGH) ? 1 : 0);
  return 0;
}

static inline gpio_state_t hal_gpio_get(unsigned int pin) {
  return gpio_get_level((gpio_num_t)pin) ? GPIO_STATE_HIGH : GPIO_STATE_LOW;
}

static inline void hal_delay_us(unsigned int time_us) { esp_rom_delay_us(time_us); }

static inline void hal_delay_ms(unsigned int time_ms) { vTaskDelay(pdMS_TO_TICKS(time_ms)); }

#define HAL_ENTER_CRITICAL() portENTER_CRITICAL(&sbw_hal_mux)
#define HAL_EXIT_CRITICAL()  portEXIT_CRITICAL(&sbw_hal_mux)

#endif /* __SBW_HAL_GLUE_H_ */
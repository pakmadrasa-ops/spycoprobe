/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/gpio.h"
#include "driver/uart.h"        // Native ESP-IDF UART peripheral driver
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"

#include "protocol.h"
#include "sbw_device.h"

// Multi-core safe hardware spinlock for high-speed SBW register logic
portMUX_TYPE sbw_hal_mux = portMUX_INITIALIZER_UNLOCKED;

// Hardware Serial Configuration
#define COM_PORT_NUM     UART_NUM_0   // Port mapped to the S3's onboard USB-to-UART chip
#define UART_BUF_SIZE    (1024)

// Pin Configurations
#define PIN_SBW_TCK      4
#define PIN_SBW_TDIO     5
#define PIN_SBW_DIR      6

#define PIN_TARGET_POWER 5
#define PIN_PROG_ENABLE  6

#define PIN_LED0         9
#define PIN_LED1         10

static TaskHandle_t led_taskhandle, cmd_taskhandle;

void led_thread(void *ptr) {
    do {
        gpio_set_level(PIN_LED0, 1);
        gpio_set_level(PIN_LED1, 0);
        vTaskDelay(pdMS_TO_TICKS(250));

        gpio_set_level(PIN_LED0, 0);
        gpio_set_level(PIN_LED1, 1);
        vTaskDelay(pdMS_TO_TICKS(250));
    } while (1);
}

/* Processes incoming commands and sets up response length payload */
int sbw_process(sbw_req_t *request, sbw_rsp_t *response) {
    switch (request->req_type) {
    case SBW_REQ_START:
        response->rc = sbw_dev_start();
        return 1;
    case SBW_REQ_STOP:
        response->rc = sbw_dev_stop();
        return 1;
    case SBW_REQ_HALT:
        response->rc = sbw_dev_halt();
        return 1;
    case SBW_REQ_RELEASE:
        response->rc = sbw_dev_release();
        return 1;
    case SBW_REQ_WRITE: {
        uint16_t tmp_data[29];
        memcpy(tmp_data, request->data, request->len * sizeof(uint16_t));
        response->rc = sbw_dev_mem_write(request->address, tmp_data, request->len);
        return 1;
    }
    case SBW_REQ_READ: {
        uint16_t tmp_data[31];
        response->rc = sbw_dev_mem_read(tmp_data, request->address, request->len);
        response->len = request->len;
        memcpy(response->data, tmp_data, request->len * sizeof(uint16_t));
        return 2 + (response->len * 2);
    }
    default:
        response->rc = SBW_RC_ERR_UNKNOWN_REQ;
        return 1;
    }
}

/* Continuously reads and routes full structured frames from the Serial Ring Buffer */
void cmd_thread(void *ptr) {
    sbw_req_t request;
    sbw_rsp_t response;
    
    sbw_pins_t pins = {
        .sbw_tck = PIN_SBW_TCK,
        .sbw_tdio = PIN_SBW_TDIO,
        .sbw_dir = PIN_SBW_DIR,
        .sbw_enable = PIN_PROG_ENABLE
    };

    sbw_dev_setup(&pins);

    uint8_t *rx_buffer_ptr = (uint8_t *)&request;

    do {
        size_t bytes_accumulated = 0;

        // Keep gathering streaming serial fragments until we construct exactly one complete protocol frame
        while (bytes_accumulated < sizeof(sbw_req_t)) {
            int len = uart_read_bytes(COM_PORT_NUM, 
                                      rx_buffer_ptr + bytes_accumulated, 
                                      sizeof(sbw_req_t) - bytes_accumulated, 
                                      portMAX_DELAY); // Blocks task cleanly when serial lines are quiet
            if (len > 0) {
                bytes_accumulated += len;
            }
        }

        // Process the validated command
        int rsp_len = sbw_process(&request, &response);

        // Stream the structured data payload back up the UART Tx transmission line
        uart_write_bytes(COM_PORT_NUM, (const char *)&response, rsp_len);
        
    } while (1);
}

void app_main(void)
{
    printf("Starting Serial COM Port SBW Infrastructure...\n");

    // Configure structural GPIOs
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_LED0) | (1ULL << PIN_LED1) | 
                        (1ULL << PIN_TARGET_POWER) | (1ULL << PIN_PROG_ENABLE),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    gpio_set_level(PIN_LED0, 0);
    gpio_set_level(PIN_LED1, 0);
    gpio_set_level(PIN_PROG_ENABLE, 0);
    gpio_set_level(PIN_TARGET_POWER, 1);

    /* 1. Hardware UART Configuration Matrix */
    uart_config_t uart_config = {
        .baud_rate = 115200,                    // Match this to your host Python/Desktop utility speed
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    // 2. Install the UART engine driver with a managed interrupt rx-ring buffer 
    ESP_ERROR_CHECK(uart_param_config(COM_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_driver_install(COM_PORT_NUM, UART_BUF_SIZE * 2, 0, 0, NULL, 0));
    
    // 3. Assign internal IO routing. 
    // Passing UART_PIN_NO_CHANGE maps them directly to the S3's dedicated internal programming hardware pins
    ESP_ERROR_CHECK(uart_set_pin(COM_PORT_NUM, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    // FreeRTOS Task Spawning
    xTaskCreate(led_thread, "LED_Task", 2048, NULL, tskIDLE_PRIORITY + 1, &led_taskhandle);
    
    // Allocated with a safe, stable stack footprint allocation (4096 bytes)
    xTaskCreate(cmd_thread, "CMD_Task", 4096, NULL, tskIDLE_PRIORITY + 2, &cmd_taskhandle);
}
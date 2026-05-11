#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <driver/uart.h>
#include <esp_log.h>
#include "serial_handler.h"
#include "config.h"

void espnow_send_uart_response(const uint8_t* data, int len);

static const char *TAG = "SERIAL_HANDLER";
static QueueHandle_t uart_to_tcp_queue = NULL;
static QueueHandle_t tcp_to_uart_queue = NULL;
static QueueHandle_t uart_espnow_queue = NULL;  // Coda SEPARATA per ESP-NOW

void serial_init(void) {
    const uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM, UART_BUF_SIZE * 2, 
                                         UART_BUF_SIZE * 2, UART_QUEUE_SIZE, 
                                         &uart_to_tcp_queue, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM, UART_TX_PIN, UART_RX_PIN, 
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    
    tcp_to_uart_queue = xQueueCreate(TCP_TO_UART_QUEUE_SIZE, sizeof(uint8_t*));
    uart_espnow_queue = xQueueCreate(50, sizeof(uart_data_t*));
}

void serial_send_data(const uint8_t *data, int length) {
    if (data && length > 0) {
        uart_write_bytes(UART_NUM, (const char*)data, length);
        uart_wait_tx_done(UART_NUM, pdMS_TO_TICKS(100));
    }
}

void serial_send_string(const char *str) {
    if (str) {
        serial_send_data((const uint8_t*)str, strlen(str));
    }
}

QueueHandle_t serial_get_queue(void) {
    return uart_to_tcp_queue;
}

bool serial_get_data(uart_data_t **data, TickType_t wait_time) {
    uart_data_t *uart_data = malloc(sizeof(uart_data_t));
    if (!uart_data) return false;
    
    uart_data->data = malloc(UART_BUF_SIZE);
    if (!uart_data->data) {
        free(uart_data);
        return false;
    }
    
    int len = uart_read_bytes(UART_NUM, uart_data->data, UART_BUF_SIZE, wait_time);
    if (len > 0) {
        uart_data->length = len;
        *data = uart_data;
        return true;
    }
    
    free(uart_data->data);
    free(uart_data);
    return false;
}

void serial_free_data(uart_data_t *data) {
    if (data) {
        if (data->data) free(data->data);
        free(data);
    }
}

// TASK ESP-NOW separato
static void espnow_sender_task(void *pvParameters) {
    uart_data_t *data = NULL;
    while (1) {
        if (xQueueReceive(uart_espnow_queue, &data, pdMS_TO_TICKS(10)) == pdTRUE) {
            if (data && data->data && data->length > 0) {
                espnow_send_uart_response(data->data, data->length);
            }
            serial_free_data(data);
        }
    }
    vTaskDelete(NULL);
}

static void serial_read_task(void *pvParameters) {
    while (1) {
        uart_data_t *data = NULL;
        if (serial_get_data(&data, pdMS_TO_TICKS(10))) {
            if (data && data->data && data->length > 0) {
                // COPIA per ESP-NOW (coda separata)
                uart_data_t *espnow_data = malloc(sizeof(uart_data_t));
                if (espnow_data) {
                    espnow_data->data = malloc(data->length);
                    if (espnow_data->data) {
                        memcpy(espnow_data->data, data->data, data->length);
                        espnow_data->length = data->length;
                        xQueueSend(uart_espnow_queue, &espnow_data, pdMS_TO_TICKS(10));
                    } else {
                        free(espnow_data);
                    }
                }
                
                // COPIA per TCP (coda separata)
                uart_data_t *tcp_data = malloc(sizeof(uart_data_t));
                if (tcp_data) {
                    tcp_data->data = malloc(data->length);
                    if (tcp_data->data) {
                        memcpy(tcp_data->data, data->data, data->length);
                        tcp_data->length = data->length;
                        xQueueSend(uart_to_tcp_queue, &tcp_data, pdMS_TO_TICKS(100));
                    } else {
                        free(tcp_data);
                    }
                }
            }
            serial_free_data(data);
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    vTaskDelete(NULL);
}

void serial_task_start(void) {
    xTaskCreate(serial_read_task, "uart_reader", SERIAL_TASK_SIZE, 
                NULL, SERIAL_TASK_PRIORITY, NULL);
    xTaskCreate(espnow_sender_task, "espnow_sender", 4096, 
                NULL, 4, NULL);
}   
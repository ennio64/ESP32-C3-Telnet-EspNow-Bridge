#ifndef ESPNOW_HANDLER_H
#define ESPNOW_HANDLER_H

#include <stdint.h>
#include <stdbool.h>

void espnow_init_handler(void);
void espnow_send_uart_response(const uint8_t* data, int len);

#endif
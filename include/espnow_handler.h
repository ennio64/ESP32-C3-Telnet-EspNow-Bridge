#ifndef ESPNOW_HANDLER_H
#define ESPNOW_HANDLER_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_ESPNOW_PEERS 4

void espnow_init_handler(void);
void espnow_send_uart_response(const uint8_t* data, int len);
bool espnow_is_paired(void);           
int espnow_get_peer_count(void);      
void espnow_clear_all_peers(void);     

#endif
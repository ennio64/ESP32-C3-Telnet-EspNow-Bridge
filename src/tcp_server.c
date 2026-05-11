#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <esp_log.h>
#include <lwip/sockets.h>
#include "tcp_server.h"
#include "serial_handler.h"
#include "config.h"
#include "grblHAL_advanced.h"

static const char *TAG = "TCP_SERVER";
static QueueHandle_t uart_data_queue = NULL;
static int client_sockets[MAX_TCP_CLIENTS];
static SemaphoreHandle_t clients_mutex = NULL;
static TaskHandle_t broadcast_task_handle = NULL;

static void init_clients(void) {
    for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
        client_sockets[i] = -1;
    }
}

static void add_client_socket(int sock) {
    int slot = -1;
    int total = 0;
    
    xSemaphoreTake(clients_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
        if (client_sockets[i] == -1) {
            client_sockets[i] = sock;
            slot = i;
            break;
        }
    }
    for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
        if (client_sockets[i] != -1) total++;
    }
    xSemaphoreGive(clients_mutex);
    
    if (slot >= 0) {
        ESP_LOGI(TAG, "Client aggiunto, slot %d, total: %d", slot, total);
    }
}

static void remove_client_socket(int sock) {
    int slot = -1;
    int total = 0;
    
    xSemaphoreTake(clients_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
        if (client_sockets[i] == sock) {
            client_sockets[i] = -1;
            slot = i;
            break;
        }
    }
    for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
        if (client_sockets[i] != -1) total++;
    }
    xSemaphoreGive(clients_mutex);
    
    if (slot >= 0) {
        ESP_LOGI(TAG, "Client rimosso, slot %d, total: %d", slot, total);
    }
}

int tcp_server_get_client_count(void) {
    int count = 0;
    xSemaphoreTake(clients_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
        if (client_sockets[i] != -1) count++;
    }
    xSemaphoreGive(clients_mutex);
    return count;
}

void tcp_broadcast_data(const uint8_t *data, int length) {
    if (!data || length <= 0) return;
    
    int active_sockets[MAX_TCP_CLIENTS];
    int active_count = 0;
    
    xSemaphoreTake(clients_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
        if (client_sockets[i] != -1) {
            active_sockets[active_count++] = client_sockets[i];
        }
    }
    xSemaphoreGive(clients_mutex);
    
    for (int i = 0; i < active_count; i++) {
        int sent = send(active_sockets[i], data, length, 0);
        if (sent < 0) {
            ESP_LOGW(TAG, "Errore invio a client %d: errno=%d", active_sockets[i], errno);
            xSemaphoreTake(clients_mutex, portMAX_DELAY);
            for (int j = 0; j < MAX_TCP_CLIENTS; j++) {
                if (client_sockets[j] == active_sockets[i]) {
                    close(client_sockets[j]);
                    client_sockets[j] = -1;
                    break;
                }
            }
            xSemaphoreGive(clients_mutex);
        }
    }
}

static void broadcast_task(void *pvParameters) {
    uart_data_t *data = NULL;
    
    while (1) {
        if (xQueueReceive(uart_data_queue, &data, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (data && data->data && data->length > 0) {
                tcp_broadcast_data(data->data, data->length);
                serial_free_data(data);
            }
        }
    }
    vTaskDelete(NULL);
}

static void client_handler_task(void *pvParameters) {
    int client_sock = *(int*)pvParameters;
    uint8_t buffer[512];
    
    while (1) {
        int len = recv(client_sock, buffer, sizeof(buffer), 0);
        if (len <= 0) {
            ESP_LOGI(TAG, "Client disconnected");
            grblHAL_advanced_check_and_send_reset();
            break;
        }
        
        serial_send_data(buffer, len);
    }
    
    remove_client_socket(client_sock);
    close(client_sock);
    free(pvParameters);
    vTaskDelete(NULL);
}

static void tcp_server_task(void *pvParameters) {
    struct sockaddr_in server_addr;
    int listen_sock;
    
    init_clients();
    
    clients_mutex = xSemaphoreCreateMutex();
    if (!clients_mutex) {
        ESP_LOGE(TAG, "Impossibile creare mutex");
        vTaskDelete(NULL);
        return;
    }
    
    listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Impossibile creare socket: errno=%d", errno);
        vTaskDelete(NULL);
        return;
    }
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(TELNET_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    if (bind(listen_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Bind fallito sulla porta %d: errno=%d", TELNET_PORT, errno);
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }
    
    if (listen(listen_sock, MAX_TCP_CLIENTS) < 0) {
        ESP_LOGE(TAG, "Listen fallita: errno=%d", errno);
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "Server Telnet avviato sulla porta %d", TELNET_PORT);
    
    if (!broadcast_task_handle && uart_data_queue) {
        xTaskCreate(broadcast_task, "tcp_broadcast", 2048, NULL, 4, &broadcast_task_handle);
    }
    
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_sock = accept(listen_sock, (struct sockaddr*)&client_addr, &client_len);
        if (client_sock < 0) {
            ESP_LOGE(TAG, "Accept fallita: errno=%d", errno);
            continue;
        }
        
        char client_ip[INET_ADDRSTRLEN];
        inet_ntoa_r(client_addr.sin_addr, client_ip, sizeof(client_ip));
        ESP_LOGI(TAG, "Nuovo client Telnet connesso da %s:%d", 
                 client_ip, ntohs(client_addr.sin_port));
        
        add_client_socket(client_sock);
        
        int *sock_ptr = malloc(sizeof(int));
        if (sock_ptr) {
            *sock_ptr = client_sock;
            xTaskCreate(client_handler_task, "client_handler", 4096, sock_ptr, 5, NULL);
        } else {
            ESP_LOGE(TAG, "Memoria esaurita, client rifiutato");
            close(client_sock);
        }
    }
    
    close(listen_sock);
    vTaskDelete(NULL);
}

void tcp_server_set_queue(QueueHandle_t queue) {
    uart_data_queue = queue;
}

void tcp_server_start(void) {
    if (!uart_data_queue) {
        ESP_LOGE(TAG, "Queue non inizializzata");
        return;
    }
    
    xTaskCreate(tcp_server_task, "tcp_server", TCP_SERVER_TASK_SIZE, 
                NULL, TCP_SERVER_PRIORITY, NULL);
}
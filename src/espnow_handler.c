#include <string.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "espnow_handler.h"
#include "espnow_config.h"
#include "serial_handler.h"
#include "my_logs.h"
#include "nvs_storage.h"

static const char *TAG = "ESPNOW";

typedef struct {
    uint8_t mac[6];
    bool active;
    int64_t last_activity_ms;   // ultimo pacchetto ricevuto (G‑code, PAIR, PONG)
    int64_t last_ping_ms;       // ultimo istante in cui abbiamo inviato un ping (0 = nessun ping in attesa)
    uint8_t ping_retries;       // numero di ping già inviati (0..ESPNOW_PING_MAX_RETRIES)
} espnow_peer_t;

static espnow_peer_t peers[MAX_ESPNOW_PEERS] = {0};
static SemaphoreHandle_t peers_mutex = NULL;
static bool espnow_initialized = false;
static int espnow_channel = 11;

// Forward declaration
static void espnow_peer_monitor_task(void *pvParameters);

static int get_wifi_channel(void) {
    uint8_t primary;
    wifi_second_chan_t second;
    if (esp_wifi_get_channel(&primary, &second) == ESP_OK) {
        return primary;
    }
    return 11;
}

static int find_peer_index(const uint8_t *mac) {
    for (int i = 0; i < MAX_ESPNOW_PEERS; i++) {
        if (peers[i].active && memcmp(peers[i].mac, mac, 6) == 0) {
            return i;
        }
    }
    return -1;
}

static int add_peer_to_list(const uint8_t *mac) {
    xSemaphoreTake(peers_mutex, portMAX_DELAY);
    
    int existing = find_peer_index(mac);
    if (existing >= 0) {
        xSemaphoreGive(peers_mutex);
        return existing;
    }
    
    for (int i = 0; i < MAX_ESPNOW_PEERS; i++) {
        if (!peers[i].active) {
            memcpy(peers[i].mac, mac, 6);
            peers[i].active = true;
            peers[i].last_activity_ms = esp_timer_get_time() / 1000;
            peers[i].last_ping_ms = 0;
            peers[i].ping_retries = 0;
            
            int total = 0;
            for (int j = 0; j < MAX_ESPNOW_PEERS; j++) {
                if (peers[j].active) total++;
            }
            ESP_LOGI(TAG, "✅ Peer aggiunto: " MACSTR " (slot %d, total: %d)", 
                     MAC2STR(mac), i, total);
            xSemaphoreGive(peers_mutex);
            return i;
        }
    }
    
    xSemaphoreGive(peers_mutex);
    ESP_LOGW(TAG, "❌ Limite peer raggiunto (%d)", MAX_ESPNOW_PEERS);
    return -1;
}

static void espnow_broadcast_to_peers(const uint8_t *data, int len) {
    if (!data || len <= 0) return;
    
    xSemaphoreTake(peers_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_ESPNOW_PEERS; i++) {
        if (peers[i].active) {
            esp_err_t err = esp_now_send(peers[i].mac, data, len);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Invio a peer %d fallito: %d", i, err);
            }
        }
    }
    xSemaphoreGive(peers_mutex);
}

int espnow_get_peer_count(void) {
    int count = 0;
    xSemaphoreTake(peers_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_ESPNOW_PEERS; i++) {
        if (peers[i].active) count++;
    }
    xSemaphoreGive(peers_mutex);
    return count;
}

bool espnow_is_paired(void) {
    return (espnow_get_peer_count() > 0);
}

void espnow_clear_all_peers(void) {
    xSemaphoreTake(peers_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_ESPNOW_PEERS; i++) {
        if (peers[i].active) {
            esp_now_del_peer(peers[i].mac);
            peers[i].active = false;
            memset(peers[i].mac, 0, 6);
            peers[i].last_activity_ms = 0;
            peers[i].last_ping_ms = 0;
            peers[i].ping_retries = 0;
        }
    }
    xSemaphoreGive(peers_mutex);
    ESP_LOGI(TAG, "✅ Tutti i peer rimossi");
}

// Gestione ricezione pacchetti
static void on_espnow_recv_cb(const esp_now_recv_info_t *recv_info, 
                               const uint8_t *data, int data_len) {
    if (!recv_info || data_len <= 0) return;
    
    const uint8_t *mac = recv_info->src_addr;
    
    // ---- HEARTBEAT PONG ----
    if (data_len == 4 && memcmp(data, CMD_PONG, 4) == 0) {
        int idx = find_peer_index(mac);
        if (idx >= 0) {
            peers[idx].last_activity_ms = esp_timer_get_time() / 1000;
            peers[idx].ping_retries = 0;
            peers[idx].last_ping_ms = 0;
            ESP_LOGD(TAG, "PONG ricevuto da " MACSTR, MAC2STR(mac));
        }
        return;
    }
    
    // ---- PAIRING ----
    if (data_len == 4 && memcmp(data, CMD_PAIR, 4) == 0) {
        ESP_LOGI(TAG, "Pairing request da " MACSTR, MAC2STR(mac));
        
        int current_channel = get_wifi_channel();
        
        if (find_peer_index(mac) < 0) {
            esp_now_peer_info_t peer = {0};
            peer.channel = current_channel;
            peer.encrypt = false;
            memcpy(peer.peer_addr, mac, 6);
            
            if (esp_now_add_peer(&peer) == ESP_OK) {
                add_peer_to_list(mac);
            }
        }
        
        const char* ok = CMD_PAIR_OK;
        esp_now_send(mac, (uint8_t*)ok, strlen(ok));
        return;
    }
    
    // ---- ACK (3 byte, ignorato) ----
    if (data_len == 3) {
        return;
    }
    
    // ---- G-CODE valido (solo da peer connessi) ----
    if (data_len >= 4 && find_peer_index(mac) >= 0) {
        // Aggiorna timestamp attività
        int idx = find_peer_index(mac);
        if (idx >= 0) {
            peers[idx].last_activity_ms = esp_timer_get_time() / 1000;
            // Resetta eventuali ping in corso
            peers[idx].ping_retries = 0;
            peers[idx].last_ping_ms = 0;
        }
        
        uint16_t seq = data[0] | (data[1] << 8);
        uint16_t line_len = data[2] | (data[3] << 8);
        
        if (line_len > 0 && line_len <= (data_len - 4) && line_len <= ESPNOW_MAX_PAYLOAD) {
            ESP_LOGI(TAG, "G-code ricevuto da " MACSTR ": seq=%d, len=%d", 
                     MAC2STR(mac), seq, line_len);
            
            uint8_t ack[3] = {data[0], data[1], 0x01};
            esp_now_send(mac, ack, 3);
            
            serial_send_data(&data[4], line_len);
            
            if (line_len > 0 && data[4 + line_len - 1] != '\n') {
                serial_send_data((uint8_t*)"\n", 1);
            }
        }
        return;
    }
    
    ESP_LOGW(TAG, "Pacchetto ignorato: len=%d", data_len);
}

// Task di monitoraggio peer (invio ping e rimozione timeout)
static void espnow_peer_monitor_task(void *pvParameters) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000)); // controlla ogni secondo
        int64_t now = esp_timer_get_time() / 1000;
        
        xSemaphoreTake(peers_mutex, portMAX_DELAY);
        for (int i = 0; i < MAX_ESPNOW_PEERS; i++) {
            if (!peers[i].active) continue;
            
            int64_t idle = now - peers[i].last_activity_ms;
            
            // Nessun ping in attesa e inattività supera la soglia → primo ping
            if (peers[i].ping_retries == 0 && idle > ESPNOW_PING_IDLE_MS) {
                ESP_LOGI(TAG, "Peer " MACSTR " idle (%lld ms), invio ping #1", 
                         MAC2STR(peers[i].mac), idle);
                esp_now_send(peers[i].mac, (uint8_t*)CMD_PING, 4);
                peers[i].ping_retries = 1;
                peers[i].last_ping_ms = now;
                continue;
            }
            
            // Se abbiamo già inviato almeno un ping
            if (peers[i].ping_retries > 0) {
                int64_t since_last_ping = now - peers[i].last_ping_ms;
                if (since_last_ping >= ESPNOW_PING_INTERVAL_MS) {
                    if (peers[i].ping_retries < ESPNOW_PING_MAX_RETRIES) {
                        ESP_LOGI(TAG, "Peer " MACSTR " nessuna risposta, invio ping #%d",
                                 MAC2STR(peers[i].mac), peers[i].ping_retries + 1);
                        esp_now_send(peers[i].mac, (uint8_t*)CMD_PING, 4);
                        peers[i].ping_retries++;
                        peers[i].last_ping_ms = now;
                    } else {
                        ESP_LOGW(TAG, "Peer " MACSTR " irraggiungibile dopo %d tentativi, rimozione",
                                 MAC2STR(peers[i].mac), ESPNOW_PING_MAX_RETRIES);
                        esp_now_del_peer(peers[i].mac);
                        peers[i].active = false;
                        memset(peers[i].mac, 0, 6);
                        peers[i].ping_retries = 0;
                        peers[i].last_ping_ms = 0;
                        peers[i].last_activity_ms = 0;
                    }
                }
            }
        }
        xSemaphoreGive(peers_mutex);
    }
    vTaskDelete(NULL);
}

// Inizializzazione ESP-NOW
void espnow_init_handler(void) {
    if (espnow_initialized) return;
    
    peers_mutex = xSemaphoreCreateMutex();
    if (!peers_mutex) {
        ESP_LOGE(TAG, "Impossibile creare mutex");
        return;
    }
    
    espnow_channel = get_wifi_channel();
    ESP_LOGI(TAG, "📡 Canale ESP-NOW: %d", espnow_channel);
    esp_wifi_set_channel(espnow_channel, WIFI_SECOND_CHAN_NONE);
    
    if (esp_now_init() != ESP_OK) {
        ESP_LOGE(TAG, "Errore ESP-NOW");
        return;
    }
    
    esp_now_register_recv_cb(on_espnow_recv_cb);
    
    uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    esp_now_peer_info_t broadcast_peer = {0};
    broadcast_peer.channel = espnow_channel;
    broadcast_peer.encrypt = false;
    memcpy(broadcast_peer.peer_addr, broadcast_mac, 6);
    esp_now_add_peer(&broadcast_peer);
    
    // Leggi la configurazione per decidere se attivare l'heartbeat (ping)
    const bridge_config_t *cfg = nvs_storage_get_config();
    bool enable_heartbeat = false;
    
    if (cfg->state_pin != -1) {                     // pin configurato
        if (cfg->client_mode != 1) {               // non Telnet Only (quindi Any o ESP-NOW Only)
            enable_heartbeat = true;
        }
    }
    // Se pin non configurato, o pin configurato ma Telnet Only -> heartbeat disabilitato
    
    if (enable_heartbeat) {
        xTaskCreate(espnow_peer_monitor_task, "espnow_monitor", 3072, NULL, 3, NULL);
        ESP_LOGI(TAG, "Heartbeat monitor attivo (pin configurato e client_mode != Telnet Only)");
    } else {
        ESP_LOGI(TAG, "Heartbeat monitor disabilitato (pin non configurato o client_mode = Telnet Only)");
    }
    
    espnow_initialized = true;
    ESP_LOGI(TAG, "✅ ESP-NOW attivo sul canale %d", espnow_channel);
    ESP_LOGI(TAG, "💡 Supporto fino a %d peer", MAX_ESPNOW_PEERS);
    ESP_LOGI(TAG, "💡 Invia 'PAIR' dal pendant per abbinare");
}

// Invio di una risposta dalla UART a tutti i peer connessi
void espnow_send_uart_response(const uint8_t* data, int len) {
    if (!data || len <= 0) return;
    if (espnow_get_peer_count() == 0) return;
    
    int send_len = len;
    if (send_len > ESPNOW_MAX_PAYLOAD) {
        send_len = ESPNOW_MAX_PAYLOAD;
        ESP_LOGW(TAG, "Pacchetto troncato da %d a %d byte", len, ESPNOW_MAX_PAYLOAD);
    }
    
    espnow_broadcast_to_peers(data, send_len);
}
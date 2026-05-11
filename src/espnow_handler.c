#include <string.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "espnow_handler.h"
#include "espnow_config.h"
#include "serial_handler.h"
#include "my_logs.h"

static const char *TAG = "ESPNOW";
static uint8_t paired_mac[6] = {0};
static bool espnow_initialized = false;
static int espnow_channel = 11;

static int get_wifi_channel(void) {
    uint8_t primary;
    wifi_second_chan_t second;
    if (esp_wifi_get_channel(&primary, &second) == ESP_OK) {
        return primary;
    }
    return 11;
}

static void on_espnow_recv_cb(const esp_now_recv_info_t *recv_info, 
                               const uint8_t *data, int data_len) {
    if (!recv_info || data_len <= 0) return;
    
    const uint8_t *mac = recv_info->src_addr;
    
    // ======================================================
    //  PAIRING
    // ======================================================
    if (data_len == 4 && memcmp(data, "PAIR", 4) == 0) {
        ESP_LOGI(TAG, "Pairing request from " MACSTR, MAC2STR(mac));
        
        int current_channel = get_wifi_channel();
        
        bool already_peer = false;
        if (paired_mac[0] != 0 && memcmp(paired_mac, mac, 6) == 0) {
            already_peer = true;
        }
        
        if (!already_peer) {
            esp_now_peer_info_t peer = {0};
            peer.channel = current_channel;
            peer.encrypt = false;
            memcpy(peer.peer_addr, mac, 6);
            
            if (esp_now_add_peer(&peer) == ESP_OK) {
                memcpy(paired_mac, mac, 6);
                ESP_LOGI(TAG, "Peer aggiunto sul canale %d", current_channel);
            }
        } else {
            ESP_LOGI(TAG, "Peer già esistente");
        }

        // ⭐ INVIA SEMPRE PAIR_OK ⭐
        const char* ok = "PAIR_OK";
        esp_now_send(mac, (uint8_t*)ok, strlen(ok));

        return;
    }
    
    // ======================================================
    //  ACK — ignoralo completamente
    // ======================================================
    if (data_len == 3) {
        return;
    }
    
    // ======================================================
    //  G-CODE valido
    // ======================================================
    if (data_len >= 4 && paired_mac[0] != 0 && memcmp(paired_mac, mac, 6) == 0) {
        uint16_t seq = data[0] | (data[1] << 8);
        uint16_t line_len = data[2] | (data[3] << 8);
        
        if (line_len > 0 && line_len <= (data_len - 4) && line_len <= 250) {
            ESP_LOGI(TAG, "G-code ricevuto: seq=%d, len=%d", seq, line_len);

            // ACK
            uint8_t ack[3] = {data[0], data[1], 0x01};
            esp_now_send(mac, ack, 3);

            // Inoltro seriale
            serial_send_data(&data[4], line_len);

            // Aggiungi newline se manca
            if (line_len > 0 && data[4 + line_len - 1] != '\n') {
                serial_send_data((uint8_t*)"\n", 1);
            }
        }
        return;
    }
    
    ESP_LOGW(TAG, "Pacchetto ignorato: len=%d", data_len);
}


void espnow_init_handler(void) {
    if (espnow_initialized) return;
    
    espnow_channel = get_wifi_channel();
    ESP_LOGI(TAG, "📡 Canale ESP-NOW: %d", espnow_channel);
    esp_wifi_set_channel(espnow_channel, WIFI_SECOND_CHAN_NONE);
    
    if (esp_now_init() != ESP_OK) {
        ESP_LOGE(TAG, "Errore ESP-NOW");
        return;
    }
    
    esp_now_register_recv_cb(on_espnow_recv_cb);
    
    // Peer broadcast per pairing
    uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    esp_now_peer_info_t broadcast_peer = {0};
    broadcast_peer.channel = espnow_channel;
    broadcast_peer.encrypt = false;
    memcpy(broadcast_peer.peer_addr, broadcast_mac, 6);
    esp_now_add_peer(&broadcast_peer);
    
    espnow_initialized = true;
    ESP_LOGI(TAG, "✅ ESP-NOW attivo sul canale %d", espnow_channel);
    ESP_LOGI(TAG, "💡 Invia 'PAIR' dal pendant per abbinare");
}

// INVIO RAW - COME FA TELNET (senza header, senza modifiche)
void espnow_send_uart_response(const uint8_t* data, int len) {
    if (!data || len <= 0 || paired_mac[0] == 0) return;
    
    // Limita a 250 byte (max ESP-NOW payload)
    int send_len = len;
    if (send_len > 250) {
        send_len = 250;
        ESP_LOGW(TAG, "Pacchetto troncato da %d a 250 byte", len);
    }
    
    // INVIO DIRETTO - come fa send() per Telnet
    esp_err_t err = esp_now_send(paired_mac, data, send_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Invio ESP-NOW fallito: %d", err);
    }
}   
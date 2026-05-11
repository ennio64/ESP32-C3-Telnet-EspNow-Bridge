#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_netif.h>
#include <esp_event.h>
#include <esp_mac.h>
#include "serial_handler.h"
#include "tcp_server.h"
#include "wifi_manager.h"
#include "espnow_handler.h"
#include "nvs_storage.h"
#include "web_server.h"
#include "config.h"
#include "my_logs.h"

static const char *TAG = "ESP32-C3-SERIAL-BRIDGE";

void app_main(void)
{
    // Disabilita TUTTI i log ESP-IDF di default (poi verranno riattivati in base a debug_level)
    esp_log_level_set("*", ESP_LOG_NONE);
    esp_log_level_set("wifi", ESP_LOG_NONE);
    esp_log_level_set("phy", ESP_LOG_NONE);
    esp_log_level_set("pp", ESP_LOG_NONE);
    esp_log_level_set("net80211", ESP_LOG_NONE);
    esp_log_level_set("esp_netif_handlers", ESP_LOG_NONE);
    esp_log_level_set("dhcpc", ESP_LOG_NONE);
    esp_log_level_set("dhcps", ESP_LOG_NONE);
    esp_log_level_set("tcpip_adapter", ESP_LOG_NONE);
    esp_log_level_set("esp_timer", ESP_LOG_NONE);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "ESP32-C3 Serial Bridge");
    ESP_LOGI(TAG, "========================================");

    // Inizializza NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Inizializza storage configurazione
    nvs_storage_init();
    const bridge_config_t *cfg = nvs_storage_get_config();
    
    // ========== APPLICA LIVELLO DEBUG ==========
    // Se debug_level è > 0, riattiva i log ESP-IDF
    if (cfg->debug_level >= 1) {
        esp_log_level_set("*", ESP_LOG_INFO);
        esp_log_level_set("wifi", ESP_LOG_INFO);
        esp_log_level_set("phy", ESP_LOG_INFO);
        esp_log_level_set("pp", ESP_LOG_INFO);
        esp_log_level_set("net80211", ESP_LOG_INFO);
        esp_log_level_set("esp_netif_handlers", ESP_LOG_INFO);
        esp_log_level_set("dhcpc", ESP_LOG_INFO);
        esp_log_level_set("dhcps", ESP_LOG_INFO);
        esp_log_level_set("tcpip_adapter", ESP_LOG_INFO);
        esp_log_level_set("esp_timer", ESP_LOG_INFO);
        
        if (cfg->debug_level >= 2) {
            esp_log_level_set("wifi", ESP_LOG_VERBOSE);
            esp_log_level_set("ESPNOW", ESP_LOG_VERBOSE);
            esp_log_level_set("WIFI_MANAGER", ESP_LOG_VERBOSE);
            ESP_LOGI(TAG, "🔊🔊 Verbose debug mode enabled (level %d)", cfg->debug_level);
        } else {
            ESP_LOGI(TAG, "🔊 Basic debug mode enabled (level %d)", cfg->debug_level);
        }
    } else {
        ESP_LOGI(TAG, "🔇 Debug mode disabled");
    }

    // Inizializza WiFi (modalità AP+STA)
    wifi_init();
    vTaskDelay(pdMS_TO_TICKS(500));

    // Imposta IP statico da configurazione
    set_static_ip_from_config();

    // Avvia Access Point con configurazione salvata
    wifi_start_ap_with_config();

    // Seleziona e connetti alla miglior rete WiFi disponibile
    wifi_auto_connect_from_nvs();

    // Inizializza ESP-NOW
    espnow_init_handler();

    // Inizializza UART e server Telnet
    serial_init();
    serial_task_start();
    tcp_server_set_queue(serial_get_queue());
    tcp_server_start();
    
    // Avvia Web Server
    web_server_start();

    // Messaggio di benvenuto
    char welcome_msg[500];
    snprintf(welcome_msg, sizeof(welcome_msg),
             "\r\n\r\n=== ESP32-C3 Serial Bridge ===\r\n"
             "Telnet server: port %d\r\n"
             "Web interface: http://%s\r\n"
             "Static IP: %s\r\n"
             "Baud rate: %d\r\n"
             "AP: %s (channel %d)\r\n"
             "Debug level: %d\r\n"
             "ESP-NOW: Send 'PAIR' to pair pendant\r\n"
             "\r\nReady...\r\n",
             TELNET_PORT, cfg->static_ip, cfg->static_ip, UART_BAUD_RATE,
             cfg->ap_ssid, cfg->ap_channel, cfg->debug_level);

    serial_send_string(welcome_msg);
    ESP_LOGI(TAG, "Bridge started - Telnet port %d, ESP-NOW active", TELNET_PORT);
    ESP_LOGI(TAG, "Web interface: http://%s", cfg->static_ip);
    ESP_LOGI(TAG, "Debug level: %d", cfg->debug_level);

    // Stampa MAC address per ESP-NOW
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    ESP_LOGI(TAG, "MAC Address (ESP-NOW): %02X:%02X:%02X:%02X:%02X:%02X", 
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}
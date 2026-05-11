#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_netif.h>
#include <esp_event.h>
#include <esp_mac.h>
#include <esp_wifi.h>
#include "serial_handler.h"
#include "tcp_server.h"
#include "wifi_manager.h"
#include "espnow_handler.h"
#include "nvs_storage.h"
#include "web_server.h"
#include "grblHAL_advanced.h"
#include "config.h"
#include "my_logs.h"

static const char *TAG = "ESP32-C3-SERIAL-BRIDGE";

// Funzione per ottenere il canale WiFi corrente della STA
static int get_sta_channel(void)
{
    wifi_second_chan_t second;
    uint8_t primary;
    if (esp_wifi_get_channel(&primary, &second) == ESP_OK)
    {
        return primary;
    }
    return 0;
}

// Funzione per stampare info sistema (sempre visibile)
static void print_system_info(const bridge_config_t *cfg)
{
    char connected_ssid[64] = "None";
    if (wifi_is_connected() && cfg->network_count > 0)
    {
        strncpy(connected_ssid, cfg->networks[0].ssid, sizeof(connected_ssid) - 1);
        connected_ssid[sizeof(connected_ssid) - 1] = '\0';
    }

    int sta_channel = get_sta_channel();

    printf("\n");
    printf("========================================\n");
    printf("         SYSTEM INFORMATION             \n");
    printf("========================================\n");
    printf("WiFi STA:      %s (%s)\n", connected_ssid, wifi_is_connected() ? "CONNECTED" : "DISCONNECTED");
    printf("STA Channel:   %d\n", sta_channel);
    printf("IP Address:    %s\n", wifi_get_ip());
    printf("AP SSID:       %s\n", cfg->ap_ssid);
    printf("AP IP:         192.168.4.1\n");
    printf("AP Channel:    %d\n", cfg->ap_channel);
    printf("Telnet Port:   %d\n", TELNET_PORT);
    printf("Web Interface: http://%s\n", cfg->static_ip);
    printf("Baud Rate:     %d\n", UART_BAUD_RATE);
    printf("Debug Level:   %d\n", cfg->debug_level);
    printf("ESP-NOW:       Send 'PAIR' to pair pendant\n");
    printf("========================================\n");

    // Stampa configurazione GrblHAL Advanced
    printf("\n");
    printf("========================================\n");
    printf("         GRBLHAL ADVANCED               \n");
    printf("========================================\n");

    // State Pin - mostra testo invece di -1
    if (cfg->state_pin == -1)
    {
        printf("State Pin:        Not configured\n");
    }
    else
    {
        printf("State Pin:        GPIO%d\n", cfg->state_pin);
    }

    printf("State Pin Mode:   %s\n", cfg->state_pin_mode == 0 ? "LOW when connected" : "HIGH when connected");
    printf("Client Source:    %s\n",
           cfg->client_mode == 0 ? "Any Client" : (cfg->client_mode == 1 ? "Telnet Only" : "ESP-NOW Only"));
    printf("Reset on Disconnect: %s\n", cfg->reset_on_disconnect ? "ENABLED" : "DISABLED");
    printf("========================================\n");
}

// Task per monitorare lo stato dei client
static void client_monitor_task(void *pvParameters)
{
    while (1)
    {
        bool telnet_connected = (tcp_server_get_client_count() > 0);
        bool espnow_connected = espnow_is_paired();

        grblHAL_advanced_update_state(telnet_connected, espnow_connected);

        vTaskDelay(pdMS_TO_TICKS(500));
    }
    vTaskDelete(NULL);
}

void app_main(void)
{
    // Attendi che il serial monitor sia pronto
    vTaskDelay(pdMS_TO_TICKS(2000));

    uint8_t mac[6];

    // ========== STAMPA FORZATA CONFIGURAZIONE NVS ==========
    printf("\n\n");
    printf("========================================\n");
    printf("ESP32-C3 SERIAL BRIDGE - BOOT\n");
    printf("========================================\n");

    // Inizializza NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Leggi configurazione direttamente da NVS
    nvs_handle_t nvs_handle;
    if (nvs_open("bridge_cfg", NVS_READONLY, &nvs_handle) == ESP_OK)
    {
        bridge_config_t boot_cfg;
        size_t size = sizeof(bridge_config_t);
        if (nvs_get_blob(nvs_handle, "config", &boot_cfg, &size) == ESP_OK)
        {
            printf("\n=== NVS CONFIGURATION ===\n");
            printf("Networks: %d\n", boot_cfg.network_count);
            for (int i = 0; i < boot_cfg.network_count && i < 10; i++)
            {
                printf("  [%d] SSID: %s\n", i, boot_cfg.networks[i].ssid);
                printf("  [%d] PWD:  %s\n", i, boot_cfg.networks[i].password);
            }
            printf("AP SSID:     %s\n", boot_cfg.ap_ssid);
            printf("AP Password: %s\n", boot_cfg.ap_password);
            printf("AP Channel:  %d\n", boot_cfg.ap_channel);
            printf("Static IP:   %s\n", boot_cfg.static_ip);
            printf("Use Static:  %s\n", boot_cfg.use_static_ip ? "YES" : "NO");
            printf("Debug Level: %d\n", boot_cfg.debug_level);
            printf("GrblHAL State Pin: %d\n", boot_cfg.state_pin);
            printf("GrblHAL Client Mode: %d\n", boot_cfg.client_mode);
            printf("GrblHAL Reset on Disconnect: %d\n", boot_cfg.reset_on_disconnect);
            printf("=========================\n");
        }
        else
        {
            printf("No config in NVS (using defaults)\n");
        }
        nvs_close(nvs_handle);
    }
    else
    {
        printf("Cannot open NVS (first boot?)\n");
    }

    // ========== DISABILITA LOG DI DEFAULT ==========
    esp_log_level_set("*", ESP_LOG_NONE);

    // ========== CARICA CONFIGURAZIONE ==========
    nvs_storage_init();
    const bridge_config_t *cfg = nvs_storage_get_config();

    // ========== APPLICA LIVELLO DEBUG ==========
    if (cfg->debug_level >= 1)
    {
        // Attiva INFO per i nostri moduli solo se debug_level >= 1
        esp_log_level_set("ESP32-C3-SERIAL-BRIDGE", ESP_LOG_INFO);
        esp_log_level_set("TCP_SERVER", ESP_LOG_INFO);
        esp_log_level_set("SERIAL_HANDLER", ESP_LOG_INFO);
        esp_log_level_set("ESPNOW", ESP_LOG_INFO);
        esp_log_level_set("WEB_SERVER", ESP_LOG_INFO);
        esp_log_level_set("WIFI_MANAGER", ESP_LOG_INFO);

        // Attiva INFO per moduli ESP-IDF
        esp_log_level_set("wifi", ESP_LOG_INFO);
        esp_log_level_set("phy", ESP_LOG_INFO);
        esp_log_level_set("esp_netif_handlers", ESP_LOG_INFO);

        if (cfg->debug_level >= 2)
        {
            // VERBOSE PER TUTTI I MODULI DEL BRIDGE
            esp_log_level_set("TCP_SERVER", ESP_LOG_VERBOSE);
            esp_log_level_set("SERIAL_HANDLER", ESP_LOG_VERBOSE);
            esp_log_level_set("ESPNOW", ESP_LOG_VERBOSE);
            esp_log_level_set("WIFI_MANAGER", ESP_LOG_VERBOSE);
            esp_log_level_set("WIFI_SELECTOR", ESP_LOG_VERBOSE);
            esp_log_level_set("NVS_STORAGE", ESP_LOG_VERBOSE);
            esp_log_level_set("WEB_SERVER", ESP_LOG_VERBOSE);
            esp_log_level_set("GRBLHAL_ADV", ESP_LOG_VERBOSE);

            // Verbose per WiFi
            esp_log_level_set("wifi", ESP_LOG_VERBOSE);
            esp_log_level_set("esp_netif_handlers", ESP_LOG_VERBOSE);

            printf("\n🔊🔊 VERBOSE DEBUG ENABLED (level %d)\n", cfg->debug_level);
        }
        else
        {
            printf("\n🔊 Basic debug enabled (level %d)\n", cfg->debug_level);
        }
    }
    else
    {
        printf("\n🔇 Debug mode disabled\n");
    }

    // Inizializza WiFi
    wifi_init();
    vTaskDelay(pdMS_TO_TICKS(500));

    set_static_ip_from_config();
    wifi_start_ap_with_config();
    wifi_auto_connect_from_nvs();

    // ========== STAMPA INFO SISTEMA (SEMPRE VISIBILE) ==========
    print_system_info(cfg);
    // ============================================================

    // Inizializza ESP-NOW
    espnow_init_handler();

    // Inizializza UART e server Telnet
    serial_init();
    serial_task_start();
    tcp_server_set_queue(serial_get_queue());
    tcp_server_start();

    // Avvia Web Server
    web_server_start();

    // Inizializza GrblHAL Advanced
    grblHAL_advanced_init();

    // Avvia task monitor per aggiornare lo stato del pin
    xTaskCreate(client_monitor_task, "client_monitor", 2048, NULL, 3, NULL);

    // Messaggio di benvenuto su seriale
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    char welcome_msg[256];
    snprintf(welcome_msg, sizeof(welcome_msg),
             "\r\n✅ System Ready - MAC: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    serial_send_string(welcome_msg);
}
#include <stdio.h>
#include <string.h>
#include <esp_http_server.h>
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include <rom/ets_sys.h>
#include "driver/rmt_tx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"

#include "ir_http_server.h"
#include "ir_codec.h"
#include "wifi.h"

#define GPIO_IR_TX   27
#define MEM_BLOCK_SYMBOLS 128
#define CHANNEL_RESOLUTION 1000000 
#define CHANNEL_DC 0.33
#define CARRIER_FREQ 33000

#define PORT                        CONFIG_PORT
#define KEEPALIVE_IDLE              CONFIG_KEEPALIVE_IDLE
#define KEEPALIVE_INTERVAL          CONFIG_KEEPALIVE_INTERVAL
#define KEEPALIVE_COUNT             CONFIG_KEEPALIVE_COUNT



static const char *TAG = "IR receiver main";

static void ir_http_server(void* pvParameters)
{   

    static httpd_handle_t server = NULL;

    rmt_channel_handle_t tx_channel;
    rmt_transmit_config_t tx_config = {
        .loop_count = 0, 
    };
    
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = GPIO_IR_TX,
        .mem_block_symbols = MEM_BLOCK_SYMBOLS,
        .resolution_hz = CHANNEL_RESOLUTION,
        .trans_queue_depth = 4, // set the number of transactions that can be pending in the background
    };

    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &tx_channel));

    rmt_carrier_config_t carrier_cfg = {
        .duty_cycle = CHANNEL_DC,
        .frequency_hz = CARRIER_FREQ,
    };

    ESP_ERROR_CHECK(rmt_apply_carrier(tx_channel, &carrier_cfg));
    ESP_ERROR_CHECK(rmt_enable(tx_channel));

    ir_jetpoint_settings_t settings;
    set_jetpoint_settings(&settings, 1, 1, 1, 25, 0, 0);
    
    rmt_encoder_handle_t jetpoint_encoder = NULL;
    ESP_ERROR_CHECK(rmt_new_jetpoint_encoder(&jetpoint_encoder));

    ir_context_t ir_context = {
        .tx_channel = &tx_channel,
        .tx_config = &tx_config,
        .encoder = &jetpoint_encoder,
        .settings = &settings,
    };

    wifi_reconnect_context_t reconnect_ctx = {
        .ir_context = &ir_context,
        .server = &server,
    };

    register_connect_disconnect_handlers((void *)&reconnect_ctx, (void *) &server);

    server = start_webserver(&ir_context);

    while(server){
        vTaskDelay(pdMS_TO_TICKS(5*1000));
    }
    
}


void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");

    wifi_connect_sta();

    xTaskCreate(ir_http_server, "http_server", 4096, NULL, 5, NULL);

    while(1){ // wifi busy loop

        EventBits_t bits = xEventGroupGetBits(wifi_event_group);

        if(bits & WIFI_CONNECTED_BIT) {
            //ESP_LOGI(TAG, "Wi-Fi connected...");
        } else {
            ESP_LOGI(TAG, "Wi-Fi connection lost.");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10*1000));
    }

}
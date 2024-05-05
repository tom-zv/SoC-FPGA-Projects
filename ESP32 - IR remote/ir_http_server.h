#ifndef IR_HTTP_SERVER_H
#define IR_HTTP_SERVER_H

#include "ir_codec.h"
#include <esp_http_server.h>
#include "driver/rmt_tx.h"

typedef struct {
    httpd_handle_t* server;   
    char *file_buf;  
    // IR context
    rmt_channel_handle_t *tx_channel;
    rmt_transmit_config_t *tx_config;
    rmt_encoder_handle_t *ir_encoder;
    ir_jetpoint_settings_t *ir_settings;
} server_context_t;


void ir_http_server_task(void* pvParameters);

esp_err_t start_ir_webserver(server_context_t *server_context);

void connect_handler(void *arg, esp_event_base_t event_base,
                            int32_t event_id, void *event_data);

void disconnect_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data);

#endif //IR_HTTP_SERVER_H
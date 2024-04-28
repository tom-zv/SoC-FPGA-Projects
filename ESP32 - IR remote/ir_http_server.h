#ifndef IR_HTTP_SERVER_H
#define IR_HTTP_SERVER_H
#include "ir_codec.h"
#include <esp_http_server.h>
#include "driver/rmt_tx.h"

typedef struct{ // Data needed for rmt tx 
    rmt_channel_handle_t *tx_channel;
    rmt_transmit_config_t *tx_config;
    rmt_encoder_handle_t *encoder;
    ir_jetpoint_settings_t *settings;
} ir_context_t;

typedef struct {
    httpd_handle_t *server;  // Handle to the web server
    ir_context_t* ir_context;  // Pointer to IR context, assuming ir_context is a structured type
} wifi_reconnect_context_t;



httpd_handle_t start_webserver(ir_context_t *ir_context);

void connect_handler(void *arg, esp_event_base_t event_base,
                            int32_t event_id, void *event_data);

void disconnect_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data);

#endif //IR_HTTP_SERVER_H
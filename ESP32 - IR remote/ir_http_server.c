#include <esp_http_server.h>
#include "driver/rmt_tx.h"
#include "ir_http_server.h"
#include "esp_log.h"

static const char *TAG = "IR receiver http server";

static esp_err_t root_get_handler(httpd_req_t *req)
{   
    ir_context_t *ir_context = (ir_context_t *)httpd_get_global_user_ctx(req->handle);
    char cur_settings_str[320];

    snprintf(cur_settings_str, sizeof(cur_settings_str),
             "<html><head><title>AC Settings</title></head>"
             "<body><h1>Current AC Settings</h1>"
             "<p>Power: %u, Mode: %u, Fan: %u, Temp: %u°C, Swing: %u, Sleep: %u</p>"
             "</body></html>",
             (unsigned int)ir_context->settings->power,
             (unsigned int)ir_context->settings->mode,
             (unsigned int)ir_context->settings->fan,
             (unsigned int)ir_context->settings->temperature,
             (unsigned int)ir_context->settings->swing,
             (unsigned int)ir_context->settings->sleep);

    char* form_html = /*html*/
    "<form action=\"/set\" method=\"post\">"
                      "Power: <input type=\"text\" name=\"power\"><br>"
                      "Mode: <input type=\"text\" name=\"mode\"><br>"
                      "Fan: <input type=\"text\" name=\"fan\"><br>"
                      "Temperature: <input type=\"text\" name=\"temperature\"><br>"
                      "Swing: <input type=\"text\" name=\"swing\"><br>"
                      "Sleep: <input type=\"text\" name=\"sleep\"><br>"
                      "<input type=\"submit\" value=\"Submit\">"
                      "</form>";
    
    char* response = malloc(strlen(cur_settings_str) + strlen(form_html) + 1);

    if (response == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    strcpy(response, cur_settings_str);
    strcat(response, form_html);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t set_post_handler(httpd_req_t *req){

    ir_context_t* ir_context = (ir_context_t*) httpd_get_global_user_ctx(req->handle);

    char *content = malloc(req->content_len + 1); 
    if (!content) {
        ESP_LOGE(TAG, "Failed to allocate memory for content");
        return ESP_ERR_NO_MEM;
    }
  
    int received = httpd_req_recv(req, content, req->content_len);
    if (received <= 0) {  
        if (received == HTTPD_SOCK_ERR_TIMEOUT) {
            ESP_LOGE(TAG, "HTTP RECV TIMEOUT");
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    content[received] = '\0';

    //httpd_resp_send(req, content, received); //echo

    int power, mode, fan, temperature, swing, sleep;

    if (sscanf(content, "power=%d&mode=%d&fan=%d&temperature=%d&swing=%d&sleep=%d", // Data sent must be in this default html-form format.
               &power, &mode, &fan, &temperature, &swing, &sleep) != 6){
        
        ESP_LOGI(TAG, "Incorrect settings format");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,"Required settings format - power=%%d&mode=%%d&fan=%%d&temperature=%d&swing=%%d&sleep=/%%d");
    }
    
    
    set_jetpoint_settings(ir_context->settings, power, mode, fan, temperature, swing, sleep);
    ESP_LOGI(TAG, "Settings applied: Power=%d, Mode=%d, Fan=%d, Temp=%d, Swing=%d, Sleep=%d\n",
                 ir_context->settings->power, ir_context->settings->mode, ir_context->settings->fan, 
                 ir_context->settings->temperature + 16, ir_context->settings->swing, ir_context->settings->sleep);

    ESP_ERROR_CHECK(rmt_transmit(*ir_context->tx_channel, *ir_context->encoder, ir_context->settings, sizeof(*ir_context->settings), ir_context->tx_config));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(*ir_context->tx_channel, portMAX_DELAY));

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);

    free(content);
    return ESP_OK;
}

static esp_err_t update_post_handler(httpd_req_t *req){

    ir_context_t* ir_context = (ir_context_t*) httpd_get_global_user_ctx(req->handle);

    char content[16];  
    int received = httpd_req_recv(req, content, sizeof(content) - 1);
    if (received <= 0) {  // Check if there is an error or if the connection was closed
        if (received == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    content[received] = '\0';

    if (!strncmp(content, "TEMP_UP", sizeof("TEMP_UP") - 1))
    {
        if(ir_context->settings->temperature < 14){
            ir_context->settings->temperature++;
        }
    }
    else if (!strncmp(content, "TEMP_DOWN", sizeof("TEMP_DOWN") - 1))
    {
        if(ir_context->settings->temperature > 0){
            ir_context->settings->temperature--;
        }
    }
    else if (!strncmp(content, "FAN_UP", sizeof("FAN_UP") - 1))
    {
        if(ir_context->settings->fan < 4){
            ir_context->settings->fan++;
        }
    }
    else if (!strncmp(content, "FAN_DOWN", sizeof("FAN_DOWN" - 1)))
    {
        if(ir_context->settings->fan > 0){
            ir_context->settings->fan--;
        }
    }
    else if (!strncmp(content, "COOL", sizeof("COOL" - 1)))
    {
        ir_context->settings->mode = 1;
    }
    else if (!strncmp(content, "HEAT", sizeof("HEAT") - 1))
    {
        ir_context->settings->mode = 2;
    }
    return ESP_OK;
}


static const httpd_uri_t uri_root = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_get_handler,
};

static const httpd_uri_t set = (httpd_uri_t){
    .uri = "/set",
    .method = HTTP_POST,
    .handler = set_post_handler,
};

static const httpd_uri_t update = (httpd_uri_t){
        .uri = "/update",
        .method = HTTP_POST,
        .handler = update_post_handler,
};

httpd_handle_t start_webserver(ir_context_t* ir_context){

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.global_user_ctx = ir_context;

    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if(httpd_start(&server,&config) == ESP_OK){
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &uri_root);
        httpd_register_uri_handler(server, &set);
        httpd_register_uri_handler(server, &update);
    }

    return server;
}

static esp_err_t stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    return httpd_stop(server);
}

void disconnect_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server) {
        ESP_LOGI(TAG, "Stopping webserver");
        if (stop_webserver(*server) == ESP_OK) {
            *server = NULL;
        } else {
            ESP_LOGE(TAG, "Failed to stop http server");
        }
    }
}
void connect_handler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data)
{
    wifi_reconnect_context_t *context = (wifi_reconnect_context_t *)arg;

    
    if (context->server == NULL) {
        ESP_LOGI(TAG, "Starting webserver");
        context->server = start_webserver(context->ir_context);
    }
}


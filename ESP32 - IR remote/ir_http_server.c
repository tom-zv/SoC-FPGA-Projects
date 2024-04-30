#include <time.h>

#include <esp_http_server.h>
#include "driver/rmt_tx.h"
#include "esp_netif_sntp.h"
#include "esp_log.h"

#include "ir_http_server.h"

#define HTTP_DATE_SIZE 30 //Enough to hold "Day, DD Mon YYYY HH:MM:SS GMT"

static const char *TAG = "IR receiver http server";

const char* get_http_date(){
    
    static char http_date[HTTP_DATE_SIZE]; // Static buffer to hold the date
    static time_t last_sync_time = -1;

    time_t now = time(NULL);
        
    if(now - last_sync_time >= 24 * 60 * 60) {
        last_sync_time = now;

        esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        esp_netif_sntp_init(&config);

        int retry = 0;
        int retry_count = 4;

        while (esp_netif_sntp_sync_wait(5000 / portTICK_PERIOD_MS) == ESP_ERR_TIMEOUT && ++retry < retry_count)
        {
            ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        }

        now = time(NULL);
        last_sync_time = now;

        esp_netif_sntp_deinit();
    }

    struct tm *gm_tm = gmtime(&now);
    strftime(http_date, HTTP_DATE_SIZE, "%a, %d %b %Y %H:%M:%S GMT", gm_tm);
    return http_date;
}


static esp_err_t root_get_handler(httpd_req_t *req)
{   
    ir_context_t *ir_context = (ir_context_t *)httpd_get_global_user_ctx(req->handle);

    char form_html_format[] = "<!DOCTYPE html>\n"
                      "<html lang=\"en\">\n"
                      "<head>\n"
                      "    <meta charset=\"UTF-8\">\n"
                      "    <title>AC Settings</title>\n"
                      "    <style>\n"
                      "        body {\n"
                      "            font-family: Arial, sans-serif;\n"
                      "            background: #f4f4f4;\n"
                      "            display: flex;\n"
                      "            justify-content: center;\n"
                      "            align-items: center;\n"
                      "            height: 100vh;\n"
                      "            margin: 0;\n"
                      "        }\n"
                      "        form {\n"
                      "            background: white;\n"
                      "            padding: 20px;\n"
                      "            border-radius: 8px;\n"
                      "            box-shadow: 0 0 10px rgba(0, 0, 0, 0.1);\n"
                      "        }\n"
                      "        input[type=\"text\"], input[type=\"submit\"] {\n"
                      "            width: 100%;\n"
                      "            padding: 8px;\n"
                      "            margin-top: 6px;\n"
                      "            margin-bottom: 16px;\n"
                      "            box-sizing: border-box; /* Includes padding and border in the element's width and height */\n"
                      "        }\n"
                      "        input[type=\"submit\"] {\n"
                      "            background-color: #4CAF50;\n"
                      "            color: white;\n"
                      "            border: none;\n"
                      "            cursor: pointer;\n"
                      "        }\n"
                      "        input[type=\"submit\"]:hover {\n"
                      "            opacity: 0.9;\n"
                      "        }\n"
                      "    </style>\n"
                      "</head>\n"
                      "<body>\n"
                      "<form action=\"/set\" method=\"post\">\n"
                      "    <h1>Current AC Settings</h1>\n"
                      "    <p>\n"
                      "        Power : %u, Mode : %u, Fan : %u, Temp: %u°C, Swing: %u, Sleep: %u\n"
                      "    </p>\n"
                      "    <label for=\"power\">Power:</label>\n"
                      "    <input type=\"text\" id=\"power\" name=\"power\"><br>\n"
                      "    <label for=\"mode\">Mode:</label>\n"
                      "    <input type=\"text\" id=\"mode\" name=\"mode\"><br>\n"
                      "    <label for=\"fan\">Fan:</label>\n"
                      "    <input type=\"text\" id=\"fan\" name=\"fan\"><br>\n"
                      "    <label for=\"temperature\">Temperature:</label>\n"
                      "    <input type=\"text\" id=\"temperature\" name=\"temperature\"><br>\n"
                      "    <label for=\"swing\">Swing:</label>\n"
                      "    <input type=\"text\" id=\"swing\" name=\"swing\"><br>\n"
                      "    <label for=\"sleep\">Sleep:</label>\n"
                      "    <input type=\"text\" id=\"sleep\" name=\"sleep\"><br>\n"
                      "    <input type=\"submit\" value=\"Submit\">\n"
                      "</form>\n"
                      "<script>\n"
                      "    document.querySelector('form').onsubmit = function(e) {\n"
                      "        if (!document.getElementById('power').value) {\n"
                      "            alert('Please enter power settings');\n"
                      "            e.preventDefault(); // Prevent form submission if power is empty\n"
                      "        }\n"
                      "    };\n"
                      "</script>\n"
                      "</body>\n"
                      "</html>\n";

    char *response = malloc(sizeof(form_html_format) + 1); 

    if (response != NULL)
    {
        sprintf(response, form_html_format,
                ir_context->settings->power,
                ir_context->settings->mode,
                ir_context->settings->fan,
                ir_context->settings->temperature + 16,
                ir_context->settings->swing,
                ir_context->settings->sleep);
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Date", get_http_date());
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);

    free(response);
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


#include <time.h>

#include <esp_https_server.h>
#include "esp_tls.h"
#include "driver/rmt_tx.h"
#include "esp_netif_sntp.h"
#include "esp_log.h"

#include "wifi.h"
#include "http_file_ops.h"
#include "ir_http_server.h"

#define SERVER_PORT 80
#define HTTP_DATE_SIZE 30 //Enough to hold "Day, DD Mon YYYY HH:MM:SS GMT"

#define GPIO_IR_TX   27
#define MEM_BLOCK_SYMBOLS 128
#define CHANNEL_RESOLUTION 1000000 
#define CHANNEL_DC 0.33
#define CARRIER_FREQ 33000

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

esp_err_t root_get_handler(httpd_req_t *req)
{   
    httpd_resp_set_hdr(req, "Date", get_http_date());
    resp_send_file(req, "index.html");

    return ESP_OK;
}

esp_err_t listdir_get_handler(httpd_req_t *req){
    
    resp_listdir(req, FS_BASE_PATH); //spiffs doesnt support dirs, list mount dir
    return ESP_OK;
}

esp_err_t set_post_handler(httpd_req_t *req){

    server_context_t* server_ctx = (server_context_t*) httpd_get_global_user_ctx(req->handle);

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

        ESP_LOGI(TAG, "Incorrect ir_settings format");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "<span style=\"color:blue;\">power=</span>"
                                                        "<span style=\"color:red;\">%d</span>"
                                                        "<span style=\"color:blue;\">&mode=</span>"
                                                        "<span style=\"color:red;\">%d</span>"
                                                        "<span style=\"color:blue;\">&fan=</span>"
                                                        "<span style=\"color:red;\">%d</span>"
                                                        "<span style=\"color:blue;\">&temperature=</span>"
                                                        "<span style=\"color:red;\">%d</span>"
                                                        "<span style=\"color:blue;\">&swing=</span>"
                                                        "<span style=\"color:red;\">%d</span>"
                                                        "<span style=\"color:blue;\">&sleep=</span>"
                                                        "<span style=\"color:red;\">/%d</span>\r\n"
                                                        "<script>setTimeout(function(){ window.location.href = '/form'; }, 5000);</script>"
                                                        "<p>You will be redirected in 5 seconds. If not, click <a href=\"/form\">here</a>.</p>");

        free(content);
        return ESP_OK;
    }

    set_jetpoint_settings(server_ctx->ir_settings, power, mode, fan, temperature, swing, sleep);
    ESP_LOGI(TAG, "Settings applied: Power=%d, Mode=%d, Fan=%d, Temp=%d, Swing=%d, Sleep=%d\n",
                 server_ctx->ir_settings->power, server_ctx->ir_settings->mode, server_ctx->ir_settings->fan, 
                 server_ctx->ir_settings->temperature + 16, server_ctx->ir_settings->swing, server_ctx->ir_settings->sleep);

    ESP_ERROR_CHECK(rmt_transmit(*server_ctx->tx_channel, *server_ctx->ir_encoder, server_ctx->ir_settings, sizeof(*server_ctx->ir_settings), server_ctx->tx_config));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(*server_ctx->tx_channel, portMAX_DELAY));

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);

    free(content);
    return ESP_OK;
}

esp_err_t update_post_handler(httpd_req_t *req){

    server_context_t* server_ctx = (server_context_t*) httpd_get_global_user_ctx(req->handle);

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
        if(server_ctx->ir_settings->temperature < 14){
            server_ctx->ir_settings->temperature++;
        }
    }
    
    else if (!strncmp(content, "TEMP_DOWN", sizeof("TEMP_DOWN") - 1))
    {
        if(server_ctx->ir_settings->temperature > 0){
            server_ctx->ir_settings->temperature--;
        }
    }
    else if (!strncmp(content, "FAN_UP", sizeof("FAN_UP") - 1))
    {
        if(server_ctx->ir_settings->fan < 4){
            server_ctx->ir_settings->fan++;
        }
    }
    else if (!strncmp(content, "FAN_DOWN", sizeof("FAN_DOWN" - 1)))
    {
        if(server_ctx->ir_settings->fan > 0){
            server_ctx->ir_settings->fan--;
        }
    }
    else if (!strncmp(content, "COOL", sizeof("COOL" - 1)))
    {
        server_ctx->ir_settings->mode = 1;
    }
    else if (!strncmp(content, "HEAT", sizeof("HEAT") - 1))
    {
        server_ctx->ir_settings->mode = 2;
    }
    return ESP_OK;
}

esp_err_t settings_get_handler(httpd_req_t *req){

    server_context_t* server_ctx = (server_context_t*) httpd_get_global_user_ctx(req->handle);
    httpd_resp_set_hdr(req, "Date", get_http_date());
    char settings[64];
    snprintf(settings, sizeof(settings), "Power: %d, Mode: %d, Fan: %d, Temp: %d, Swing: %d, Sleep: %d",
             server_ctx->ir_settings->power, server_ctx->ir_settings->mode, server_ctx->ir_settings->fan, server_ctx->ir_settings->temperature + 16, server_ctx->ir_settings->swing, server_ctx->ir_settings->sleep);

    httpd_resp_send(req, settings, strlen(settings));
    return ESP_OK;
}

esp_err_t upload_post_handler(httpd_req_t *req){

    httpd_resp_set_hdr(req, "Date", get_http_date());
    upload_file(req);
    return ESP_OK;
}

esp_err_t common_get_handler(httpd_req_t *req){
    httpd_resp_set_hdr(req, "Date", get_http_date());
    resp_send_file(req, req->uri + 1); // skip the leading '/'
    return ESP_OK;
}

esp_err_t setup_server_context(server_context_t* server_context){

    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = GPIO_IR_TX,
        .mem_block_symbols = MEM_BLOCK_SYMBOLS,
        .resolution_hz = CHANNEL_RESOLUTION,
        .trans_queue_depth = 4, // set the number of transactions that can be pending in the background
    };

    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, server_context->tx_channel));

    rmt_carrier_config_t carrier_cfg = {
        .duty_cycle = CHANNEL_DC,
        .frequency_hz = CARRIER_FREQ,
    };

    ESP_ERROR_CHECK(rmt_apply_carrier(*server_context->tx_channel, &carrier_cfg));
    ESP_ERROR_CHECK(rmt_enable(*server_context->tx_channel));

    set_jetpoint_settings(server_context->ir_settings, 1, 1, 1, 25, 0, 0);
    ESP_ERROR_CHECK(rmt_new_jetpoint_encoder(server_context->ir_encoder));

    server_context->tx_config->loop_count = 0;

    return ESP_OK;
}

#ifdef CONFIG_ESP_TLS_USING_MBEDTLS
static void print_peer_cert_info(const mbedtls_ssl_context *ssl)
{
    const mbedtls_x509_crt *cert;
    const size_t buf_size = 1024;
    char *buf = calloc(buf_size, sizeof(char));
    if (buf == NULL) {
        ESP_LOGE(TAG, "Out of memory - Callback execution failed!");
        return;
    }

    // Logging the peer certificate info
    cert = mbedtls_ssl_get_peer_cert(ssl);
    if (cert != NULL) {
        mbedtls_x509_crt_info((char *) buf, buf_size - 1, "    ", cert);
        ESP_LOGI(TAG, "Peer certificate info:\n%s", buf);
    } else {
        ESP_LOGW(TAG, "Could not obtain the peer certificate!");
    }

    free(buf);
}
#endif
static void https_server_user_callback(esp_https_server_user_cb_arg_t *user_cb)
{
    ESP_LOGI(TAG, "User callback invoked!");
#ifdef CONFIG_ESP_TLS_USING_MBEDTLS
    mbedtls_ssl_context *ssl_ctx = NULL;
#endif
    switch(user_cb->user_cb_state) {
        case HTTPD_SSL_USER_CB_SESS_CREATE:
            ESP_LOGD(TAG, "At session creation");
            // Logging the socket FD
            int sockfd = -1;
            esp_err_t esp_ret;
            esp_ret = esp_tls_get_conn_sockfd(user_cb->tls, &sockfd);
            if (esp_ret != ESP_OK) {
                ESP_LOGE(TAG, "Error in obtaining the sockfd from tls context");
                break;
            }
            ESP_LOGI(TAG, "Socket FD: %d", sockfd);
#ifdef CONFIG_ESP_TLS_USING_MBEDTLS
            ssl_ctx = (mbedtls_ssl_context *) esp_tls_get_ssl_context(user_cb->tls);
            if (ssl_ctx == NULL) {
                ESP_LOGE(TAG, "Error in obtaining ssl context");
                break;
            }
            // Logging the current ciphersuite
            ESP_LOGI(TAG, "Current Ciphersuite: %s", mbedtls_ssl_get_ciphersuite(ssl_ctx));
#endif
            break;

        case HTTPD_SSL_USER_CB_SESS_CLOSE:
            ESP_LOGD(TAG, "At session close");
#ifdef CONFIG_ESP_TLS_USING_MBEDTLS
            // Logging the peer certificate
            ssl_ctx = (mbedtls_ssl_context *) esp_tls_get_ssl_context(user_cb->tls);
            if (ssl_ctx == NULL) {
                ESP_LOGE(TAG, "Error in obtaining ssl context");
                break;
            }
            print_peer_cert_info(ssl_ctx);
#endif
            break;
        default:
            ESP_LOGE(TAG, "Illegal state!");
            return;
    }
}

esp_err_t start_ir_webserver(server_context_t* server_context)
{
    // SSL config
    httpd_ssl_config_t conf = HTTPD_SSL_CONFIG_DEFAULT();
    conf.user_cb = https_server_user_callback;
    // Load embedded server certificates
    extern const unsigned char servercert_start[] asm("_binary_servercert_pem_start");
    extern const unsigned char servercert_end[]   asm("_binary_servercert_pem_end");
    conf.servercert = servercert_start;
    conf.servercert_len = servercert_end - servercert_start;

    extern const unsigned char prvtkey_pem_start[] asm("_binary_prvtkey_pem_start");
    extern const unsigned char prvtkey_pem_end[]   asm("_binary_prvtkey_pem_end");
    conf.prvtkey_pem = prvtkey_pem_start;
    conf.prvtkey_len = prvtkey_pem_end - prvtkey_pem_start;

    conf.httpd.lru_purge_enable = true;
    conf.httpd.server_port = SERVER_PORT;
    conf.httpd.max_uri_handlers = 10;
    conf.httpd.uri_match_fn = httpd_uri_match_wildcard;
    
    char* file_buf = malloc(FILE_BUF_SIZE); 
    if (!file_buf) {
        ESP_LOGE(TAG, "Failed to allocate memory for file storage");
        return ESP_ERR_NO_MEM;
    }

    server_context->file_buf = file_buf;
    conf.httpd.global_user_ctx = server_context;

    // uri handlers
    httpd_uri_t uri_root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
    };
    httpd_uri_t set = {
        .uri = "/set",
        .method = HTTP_POST,
        .handler = set_post_handler,
    };
    httpd_uri_t update = {
        .uri = "/update",
        .method = HTTP_POST,
        .handler = update_post_handler,
    };
    httpd_uri_t list_dir = {
        .uri = "/listdir",
        .method = HTTP_GET,
        .handler = listdir_get_handler,
    };
    httpd_uri_t settings = {
        .uri = "/ac_settings",
        .method = HTTP_GET,
        .handler = settings_get_handler,
    };
    httpd_uri_t upload = {
        .uri = "/upload/*",
        .method = HTTP_POST,
        .handler = upload_post_handler,
    };
    httpd_uri_t common_get_request = {
    .uri = "/*", 
    .method = HTTP_GET,
    .handler = common_get_handler,
    };

    ESP_LOGI(TAG, "Starting server on port: '%d'", conf.httpd.server_port);

    esp_err_t ret = httpd_ssl_start(server_context->server, &conf);

    if(ret == ESP_OK){
        //ESP_LOGI(TAG, "Server started successfuly");
        httpd_register_uri_handler(*server_context->server, &uri_root);
        httpd_register_uri_handler(*server_context->server, &set);
        httpd_register_uri_handler(*server_context->server, &update);
        httpd_register_uri_handler(*server_context->server, &list_dir);
        httpd_register_uri_handler(*server_context->server, &settings);
        httpd_register_uri_handler(*server_context->server, &upload);
        httpd_register_uri_handler(*server_context->server, &common_get_request);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to start server: %s", esp_err_to_name(ret));
        free(file_buf); 
        server_context->file_buf = NULL;
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t stop_ir_webserver(server_context_t* server_context)
{
    free(server_context->file_buf);
    rmt_disable(*server_context->tx_channel);
    rmt_del_channel(*server_context->tx_channel);
    return httpd_stop(*server_context->server);
}

void disconnect_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    server_context_t* server_ctx = (server_context_t*) arg;
    if (!server_ctx->server) {
        ESP_LOGI(TAG, "Stopping webserver");
        if (stop_ir_webserver(server_ctx) == ESP_OK) {
            server_ctx->server = NULL;
        } else {
            ESP_LOGE(TAG, "Failed to stop http server");
        }
    }
}
void connect_handler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data)
{
    server_context_t *server_ctx = (server_context_t *)arg;

    if (server_ctx->server == NULL) {
        ESP_LOGI(TAG, "Starting webserver");
        setup_server_context(server_ctx);
        start_ir_webserver(server_ctx);
    }
}

void ir_http_server_task(void* pvParameters)
{   
    httpd_handle_t server = NULL;
    rmt_channel_handle_t tx_channel = NULL;
    rmt_transmit_config_t tx_config = {0}; 
    ir_jetpoint_settings_t settings = {0};
    rmt_encoder_handle_t jetpoint_encoder = NULL;
    
    server_context_t server_context = {
        .server = &server,
        .file_buf = NULL,
        .tx_channel = &tx_channel,
        .tx_config = &tx_config,
        .ir_encoder = &jetpoint_encoder,
        .ir_settings = &settings,
    };

    mount_spiffs_storage();

    if(setup_server_context(&server_context) != ESP_OK){
        ESP_LOGE(TAG, "Failed to setup server context");
        return;
    }

    register_connect_disconnect_handlers((void *)&server_context);
    start_ir_webserver(&server_context);

    while(1){
        vTaskDelay(pdMS_TO_TICKS(5*1000));
    }
    stop_ir_webserver(&server_context);
}

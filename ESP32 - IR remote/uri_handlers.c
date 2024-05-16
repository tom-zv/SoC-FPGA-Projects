#include <cJSON.h>
#include "esp_log.h"
#include "uri_handlers.h"
#include "server_schedule.h"

const static char* TAG = "uri handlers";

esp_err_t root_get_handler(httpd_req_t *req)
{   
    httpd_resp_set_hdr(req, "Date", get_http_date());
    resp_send_file(req, "index.html");

    return ESP_OK;
}

esp_err_t listdir_get_handler(httpd_req_t *req){
    
    resp_listdir(req, FS_BASE_PATH); // spiffs doesnt support dirs, list mount dir
    return ESP_OK;
}

esp_err_t settings_get_handler(httpd_req_t *req){
    tx_context* tx_ctx = (tx_context*)req->user_ctx;

    httpd_resp_set_hdr(req, "Date", get_http_date());
    char settings_json[128];
    snprintf(settings_json, sizeof(settings_json), "{\"power\":%d, \"mode\": %d, \"fan\": %d, \"temp\": %d, \"swing\": %d, \"sleep\": %d}",
            tx_ctx->ir_settings->power_state, tx_ctx->ir_settings->mode, tx_ctx->ir_settings->fan, tx_ctx->ir_settings->temperature + 16, tx_ctx->ir_settings->swing, tx_ctx->ir_settings->sleep);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, settings_json, strlen(settings_json));
    return ESP_OK;
}

esp_err_t schedule_get_handler(httpd_req_t *req){

    ac_scheduler_context* schedule_ctx = (ac_scheduler_context*)req->user_ctx;

    schedule_entry *schedule_table = schedule_ctx->schedule_table;
    cJSON *root = cJSON_CreateArray();

    for (int i = 0; i < MAX_SCHEDULE_ENTRIES; i++)
    {
        if(schedule_table[i].is_valid == 0) continue;

        cJSON *entry = cJSON_CreateObject();

        if (entry == NULL)
        {
            cJSON_Delete(root);
            return ESP_ERR_NO_MEM;
        }

        cJSON_AddItemToArray(root, entry);
        cJSON_AddNumberToObject(entry, "hour_on", schedule_table[i].hour_on);
        cJSON_AddNumberToObject(entry, "minute_on", schedule_table[i].minute_on);
        cJSON_AddNumberToObject(entry, "hour_off", schedule_table[i].hour_off);
        cJSON_AddNumberToObject(entry, "minute_off", schedule_table[i].minute_off);
        cJSON_AddNumberToObject(entry, "wdaybitmask", schedule_table[i].days_of_week);
        cJSON_AddBoolToObject(entry, "is_on", schedule_table[i].is_on);
        cJSON_AddBoolToObject(entry, "index", i);
    }

    char *json_string = cJSON_Print(root);
    cJSON_Delete(root);

    httpd_resp_set_hdr(req, "Date", get_http_date());    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, strlen(json_string));
    return ESP_OK;
}

esp_err_t update_post_handler(httpd_req_t *req){

    tx_context* tx_ctx = (tx_context*)req->user_ctx;

    char content[16];  
    int received = httpd_req_recv(req, content, sizeof(content) - 1);
    if (received <= 0) {  // Check if there is an error or if the connection was closed
        if (received == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    content[received] = '\0';

    if(!strncmp(content, "POWER", sizeof("POWER") - 1)){
        tx_ctx->ir_settings->power = 1;
    }
    else if(!strncmp(content, "SWING", sizeof("SWING") - 1)){
        tx_ctx->ir_settings->swing = !tx_ctx->ir_settings->swing;
    }
    else if (!strncmp(content, "TEMP_UP", sizeof("TEMP_UP") - 1))
    {
        if(tx_ctx->ir_settings->temperature < 14){
            tx_ctx->ir_settings->temperature++;
        }
    }
    else if (!strncmp(content, "TEMP_DOWN", sizeof("TEMP_DOWN") - 1))
    {
        if(tx_ctx->ir_settings->temperature > 0){
            tx_ctx->ir_settings->temperature--;
        }
    }
    else if (!strncmp(content, "FAN_UP", sizeof("FAN_UP") - 1))
    {   
        // overflows to 0 since its a 2-bit field
        if(tx_ctx->ir_settings->fan < 4){
            tx_ctx->ir_settings->fan++;
        }
    }
    else if (!strncmp(content, "FAN_DOWN", sizeof("FAN_DOWN" - 1)))
    {
        if(tx_ctx->ir_settings->fan > 0){
            tx_ctx->ir_settings->fan--;
        }
    }
    else if (!strncmp(content, "COOL", sizeof("COOL" - 1)))
    {
        tx_ctx->ir_settings->mode = 1;
    }
    else if (!strncmp(content, "HEAT", sizeof("HEAT") - 1))
    {
        tx_ctx->ir_settings->mode = 2;
    }
    else if(!strncmp(content, "AUTO", sizeof("AUTO") - 1)){
        tx_ctx->ir_settings->mode = 3;
    }
    else if(!strncmp(content, "DRY", sizeof("DRY") - 1)){
        tx_ctx->ir_settings->mode = 4;
    }
    else if(!strncmp(content, "FAN", sizeof("FAN") - 1)){
        tx_ctx->ir_settings->mode = 5;
    }

    // Send new settings to AC
    transmit_ir(tx_ctx->tx_channel, tx_ctx->ir_encoder, tx_ctx->ir_settings, tx_ctx->tx_config);
    
    if(tx_ctx->ir_settings->power == 1){ // power isnt a persistent state, it toggles the AC.
        tx_ctx->ir_settings->power_state = !tx_ctx->ir_settings->power_state;
        tx_ctx->ir_settings->power = 0;
    }

    httpd_resp_send(req, NULL, 0);

    return ESP_OK;
}

esp_err_t upload_post_handler(httpd_req_t *req){
    httpd_resp_set_hdr(req, "Date", get_http_date());
    upload_file(req);
    return ESP_OK;
}

esp_err_t schedule_post_handler(httpd_req_t *req){

    ac_scheduler_context *schedule_ctx = (ac_scheduler_context*)req->user_ctx;
    xSemaphoreTake(*schedule_ctx->schedule_uri_bSemaphore, portMAX_DELAY); // released in scheduler task. protects both data buffer and active uri handler access 
    schedule_ctx->active_uri_handler = xTaskGetCurrentTaskHandle();

    size_t bytes_read = 0;
    bytes_read = httpd_req_recv(req, schedule_ctx->schedule_data, SCHEDULE_BUF_SIZE);
    schedule_ctx->schedule_data[bytes_read] = '\0';
    
    int action;
    if(!strncmp(req->uri, "/schedule/update", sizeof("schedule/update") - 1)){
        action = EVENT_SCHED_UPDATE;
    }
    else if(!strncmp(req->uri, "/schedule/delete", sizeof("schedule/del") - 1)){
        action = EVENT_SCHED_DEL;
    }
    else{
        httpd_resp_send_404(req); // invalid uri
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "state changed = %d", action);
    xTaskNotify(schedule_ctx->scheduler_task, action, eSetValueWithOverwrite);

    uint32_t sched_err_notification;
    xTaskNotifyWait(0, 0, &sched_err_notification, portMAX_DELAY);

    httpd_resp_set_hdr(req, "Date", get_http_date());

    if(sched_err_notification == ERR_SCHED_FULL){
        httpd_resp_set_status(req, "422 Unprocessable Entity");
        httpd_resp_send(req, "Schedule full", 0);
        return ESP_OK;
    }

    else if(sched_err_notification == ERR_WRONG_FORMAT){
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Wrong format", 0);
        return ESP_OK;
    }
    
    httpd_resp_send(req, NULL, 0);
    schedule_ctx->active_uri_handler = NULL;
    return ESP_OK;
}

esp_err_t common_get_handler(httpd_req_t *req){
    httpd_resp_set_hdr(req, "Date", get_http_date());
    resp_send_file(req, req->uri + 1); // skip the leading '/'
    return ESP_OK;
}

esp_err_t register_uri_handlers(server_context *server_ctx){

    tx_context *tx_ctx = server_ctx->tx_ctx;

    httpd_uri_t uri_root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
        .user_ctx = server_ctx->file_buf,
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
        .user_ctx = tx_ctx,
    };
    httpd_uri_t schedule_table= {
        .uri = "/schedule_table",
        .method = HTTP_GET,
        .handler = schedule_get_handler,
        .user_ctx = server_ctx->scheduler_ctx,
    };
    httpd_uri_t update = {
        .uri = "/update",
        .method = HTTP_POST,
        .handler = update_post_handler,
        .user_ctx = tx_ctx,
    };
    httpd_uri_t upload = {
        .uri = "/upload/*",
        .method = HTTP_POST,
        .handler = upload_post_handler,
        .user_ctx = server_ctx->file_buf,
    };
    httpd_uri_t schedule_post={
        .uri = "/schedule/*",
        .method = HTTP_POST,
        .handler = schedule_post_handler,
        .user_ctx = server_ctx->scheduler_ctx,
    };
    httpd_uri_t common_get_request = {
        .uri = "/*", 
        .method = HTTP_GET,
        .handler = common_get_handler,
        .user_ctx = server_ctx->file_buf,
    };

    httpd_register_uri_handler(*server_ctx->server, &uri_root);
    httpd_register_uri_handler(*server_ctx->server, &update);
    httpd_register_uri_handler(*server_ctx->server, &list_dir);
    httpd_register_uri_handler(*server_ctx->server, &settings);
    httpd_register_uri_handler(*server_ctx->server, &schedule_table);
    httpd_register_uri_handler(*server_ctx->server, &upload);
    httpd_register_uri_handler(*server_ctx->server, &schedule_post);
    httpd_register_uri_handler(*server_ctx->server, &common_get_request);
    
    return ESP_OK;

}





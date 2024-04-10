#include "ir_electra_encoder.h"

#include "freertos/FreeRTOS.h"

#include "esp32/rom/ets_sys.h"

#include "esp_check.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"

#define DATA_BITS 34

static const char *TAG = "Encoder";

typedef struct
{
    int state;
    int repeat_count;
    rmt_encoder_t base;
    rmt_encoder_t *bytes_encoder;
    rmt_encoder_t *copy_encoder;
    rmt_symbol_word_t leading_symbol;
    rmt_symbol_word_t trailing_symbol;

} rmt_ir_electra_encoder_t;

void set_electra_settings(ir_electra_settings *settings, bool power, uint8_t mode, uint8_t fan, uint8_t temperature, bool swing, bool sleep)
{

    settings->power = power ? 1 : 0;    // credit to barakwei- https://github.com/barakwei/IRelectra
    settings->mode = mode;              // for configuration structure
    settings->fan = fan;
    settings->temperature = (temperature - 15) & 0x0F; // Assuming temperature is offset by 15
    settings->swing = swing ? 1 : 0;
    settings->sleep = sleep ? 1 : 0;
    settings->ones1 = 1; //
}

void encode_electra_settings(rmt_symbol_word_t *symbols, ir_electra_settings *settings)
{
    symbols[0] = (rmt_symbol_word_t){ // leading symbol
        .level0 = 0,
        .duration0 = 3000,
        .level1 = 1,
        .duration1 = 3000,
    };

    
    for (int i = DATA_BITS-1; i >= 0; i--)  // MSB -> LSB
    {
        bool bit = (settings->raw >> i) & 1;
        symbols[i] = (rmt_symbol_word_t){
            .level0 = bit ? 1 : 0, // "0" -> "01" and "1" -> "10" for Manchester encoding
            .duration0 = 1000,
            .level1 = bit ? 0 : 1,
            .duration1 = 1000,
        };
    }
}

size_t rmt_encode_cmd(rmt_encoder_t *encoder, rmt_channel_handle_t channel, const void *primary_data, size_t data_size, rmt_encode_state_t *ret_state)
{

    rmt_ir_electra_encoder_t *enc = __containerof(encoder, rmt_ir_electra_encoder_t, base);
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    rmt_encode_state_t state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;
    enc->repeat_count = 0;
    rmt_symbol_word_t symbols[DATA_BITS + 1];
    encode_electra_settings(symbols, (ir_electra_settings *)primary_data);
    
    switch (enc->state)
    {
    case 0: // send repeating code
        while (enc->repeat_count < 3)
        {
            encoded_symbols += enc->copy_encoder->encode(enc->copy_encoder, channel, symbols, sizeof(symbols), &session_state);
            enc->repeat_count++;
            //ets_printf("DATA #%d: encoded %d symbols\n", enc->repeat_count, encoded_symbols);

            if (session_state & RMT_ENCODING_COMPLETE)
            {
                enc->state = 1; 
            }
            if (session_state & RMT_ENCODING_MEM_FULL)
            {
                state |= RMT_ENCODING_MEM_FULL;
                goto out; // yield if there's no free space to put other encoding artifacts
            }
        }
        enc->repeat_count = 0;
        enc->state = 1;

    case 1: // Send trailing code

        encoded_symbols += enc->copy_encoder->encode(enc->copy_encoder, channel, &enc->trailing_symbol,
                                                     sizeof(rmt_symbol_word_t), &session_state);

        if (session_state & RMT_ENCODING_COMPLETE)
        {
            enc->state = RMT_ENCODING_RESET;
            state |= RMT_ENCODING_COMPLETE;
        }
        if (session_state & RMT_ENCODING_MEM_FULL)
        {
            state |= RMT_ENCODING_MEM_FULL;
            goto out;
        }
    }
out:
    //ets_printf("TOTAL: encoded %d symbols\n", encoded_symbols);
    *ret_state = state;
    return encoded_symbols;
}

static esp_err_t rmt_del_electra_encoder(rmt_encoder_t *encoder)
{
    rmt_ir_electra_encoder_t *electra_encoder = __containerof(encoder, rmt_ir_electra_encoder_t, base);
    rmt_del_encoder(electra_encoder->copy_encoder);
    rmt_del_encoder(electra_encoder->bytes_encoder);
    free(electra_encoder);
    return ESP_OK;
}

static esp_err_t rmt_reset_electra_encoder(rmt_encoder_t *encoder)
{
    rmt_ir_electra_encoder_t *electra_encoder = __containerof(encoder, rmt_ir_electra_encoder_t, base);
    rmt_encoder_reset(electra_encoder->copy_encoder);
    rmt_encoder_reset(electra_encoder->bytes_encoder);
    electra_encoder->state = RMT_ENCODING_RESET;
    return ESP_OK;
}

esp_err_t rmt_new_encoder(rmt_encoder_handle_t *ret_encoder){

    esp_err_t ret = ESP_OK;
    rmt_ir_electra_encoder_t *cmd_encoder = NULL;
    cmd_encoder = rmt_alloc_encoder_mem(sizeof(rmt_ir_electra_encoder_t));
    
    cmd_encoder->base.encode = rmt_encode_cmd;
    cmd_encoder->base.del = rmt_del_electra_encoder;
    cmd_encoder->base.reset = rmt_reset_electra_encoder;

    rmt_copy_encoder_config_t copy_encoder_config = {};
    ESP_GOTO_ON_ERROR(rmt_new_copy_encoder(&copy_encoder_config, &cmd_encoder->copy_encoder), err, TAG,
                      "create copy encoder failed");

    cmd_encoder->leading_symbol = (rmt_symbol_word_t) {
        .level0 = 0,
        .duration0 = 3000, // add [*resolution / wanted resolution], for duration independent on channel resolution.
        .level1 = 1,
        .duration1 = 3000, 
    };

    rmt_bytes_encoder_config_t bytes_encoder_config = { 
        .bit0 = {
            .level0 = 0, 
            .duration0 = 1000,
            .level1 = 1,
            .duration1 = 1000,  
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = 1000,
            .level1 = 0, 
            .duration1 = 1000,
        },
    };

    ESP_GOTO_ON_ERROR(rmt_new_bytes_encoder(&bytes_encoder_config, &cmd_encoder->bytes_encoder), err, TAG, "create copy encoder failed");

    *ret_encoder = &cmd_encoder->base;
    return ESP_OK;

err:
    if (cmd_encoder)
    {
        if (cmd_encoder->bytes_encoder)
        {
            rmt_del_encoder(cmd_encoder->bytes_encoder);
        }
        if (cmd_encoder->copy_encoder)
        {
            rmt_del_encoder(cmd_encoder->copy_encoder);
        }
        free(cmd_encoder);
    }
    return ret;
}
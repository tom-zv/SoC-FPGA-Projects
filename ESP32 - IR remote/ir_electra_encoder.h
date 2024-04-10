#include <stdint.h>

#include "driver/rmt_tx.h"

typedef union {
    struct
    {
        // Data sent with 'power' going first
        uint64_t zeros1 : 1;      // 0
        uint64_t ones1 : 1;       // 1
        uint64_t zeros2 : 16;     // 2-17
        uint64_t sleep : 1;       // 18
        uint64_t temperature : 4; // 19-22
        uint64_t zeros3 : 2;      // 23-24
        uint64_t swing : 1;       // 25
        uint64_t zeros4 : 2;      // 26-27
        uint64_t fan : 2;         // 28-29
        uint64_t mode : 3;        // 30-32
        uint64_t power : 1;       // 33     
        uint64_t padding : 30;
    };
    uint64_t raw;

}ir_electra_settings;


esp_err_t rmt_new_encoder(rmt_encoder_handle_t *ret_encoder);

void set_electra_settings(ir_electra_settings* settings, bool power, uint8_t mode, uint8_t fan, uint8_t temperature, bool swing, bool sleep);
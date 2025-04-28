#pragma once

#include "esp_err.h"

#include "host/ble_gatt.h"

#ifdef _cplusplus
extern "c"
{
#endif

    void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);
    void ble_keyboard_send_input_report(uint8_t *report_data, uint8_t report_type);
    esp_err_t gatt_svc_init(void);

#ifdef _cplusplus
}
#endif

#include "gatt_svc.h"

#include "host/ble_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "esp_log.h" // Assuming ESP-IDF for ESP_LOGI and ESP_LOGE

#include <assert.h> // For assert

static const char *TAG = "ble gatt svc";

extern uint16_t gatt_connection_handle; // Defined in main application

static uint8_t protocol_mode_Report[] = { 0x01 }; // Boot Protocol Mode by default

/* HID Service UUID */
static const ble_uuid16_t hid_service_svc_uuid = BLE_UUID16_INIT(0x1812);

/* HID Characteristic UUIDs */
static const ble_uuid16_t hid_device_Information_uuid = BLE_UUID16_INIT(0x2a4a);
static const ble_uuid16_t hid_report_map_uuid = BLE_UUID16_INIT(0x2a4b);
static const ble_uuid16_t hid_control_Poin_uuid = BLE_UUID16_INIT(0x2a4c);
static const ble_uuid16_t hid_data_reporting_uuid = BLE_UUID16_INIT(0x2a4d); // Used for both Key Press and Consumer Press reports. Consider separate UUIDs for clarity if needed.
static const ble_uuid16_t hid_Protocol_mode_control_uuid = BLE_UUID16_INIT(0x2a4e);
static const ble_uuid16_t hid_report_ref_desc_uuid = BLE_UUID16_INIT(0x2908);

/* Characteristic Value Handles */
static uint16_t Control_Point_chr_val_handle;
static uint16_t output_Report_chr_val_handle;
static uint16_t Key_Press_chr_val_handle;
static uint16_t Consumer_Press_chr_val_handle;

/* Access callback function prototypes */
static int gatt_svr_chr_access_hid_info(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
static int gatt_svr_chr_access_report_map(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
static int gatt_svr_chr_access_control_Point(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
static int gatt_svr_chr_access_input_report(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
static int gatt_svr_chr_access_output_report(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
static int gatt_svr_chr_access_protocol_mode(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
static int gatt_svr_dsc_access_report_ref_key(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
static int gatt_svr_dsc_access_report_ref_consumer(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
static int gatt_svr_dsc_access_report_ref_output(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);

/* GATT service definition */
static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        /*** Service: HID Service */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &hid_service_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                /*** Characteristic: HID Information */
                .uuid = &hid_device_Information_uuid.u,
                .access_cb = gatt_svr_chr_access_hid_info,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                /*** Characteristic: Report Map */
                .uuid = &hid_report_map_uuid.u,
                .access_cb = gatt_svr_chr_access_report_map,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                /*** Characteristic: Control Point */
                .uuid = &hid_control_Poin_uuid.u,
                .access_cb = gatt_svr_chr_access_control_Point,
                .flags = BLE_GATT_CHR_F_WRITE,
                .val_handle = &Control_Point_chr_val_handle,
            },
            {
                /*** Characteristic: Input Report (Key Press) - Report ID 1 */
                .uuid = &hid_data_reporting_uuid.u, // Using same UUID for different reports, distinguished by Report IDs. Consider separate UUIDs for clarity in larger services.
                .access_cb = gatt_svr_chr_access_input_report,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &Key_Press_chr_val_handle,
                .descriptors = (struct ble_gatt_dsc_def[]){
                    {
                        /*** Descriptor: Report Reference for Key Press Report (Report ID = 1, Input) */
                        .uuid = &hid_report_ref_desc_uuid.u,
                        .access_cb = gatt_svr_dsc_access_report_ref_key,
                        .att_flags = BLE_ATT_F_READ,
                    },
                    {
                        0, /* End of descriptors */
                    },
                },
            },
            {
                /*** Characteristic: Input Report (Consumer Control) - Report ID 2 */
                .uuid = &hid_data_reporting_uuid.u, // Using same UUID for different reports, distinguished by Report IDs. Consider separate UUIDs for clarity in larger services.
                .access_cb = gatt_svr_chr_access_input_report,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &Consumer_Press_chr_val_handle,
                .descriptors = (struct ble_gatt_dsc_def[]){
                    {
                        /*** Descriptor: Report Reference for Consumer Report (Report ID = 2, Input) */
                        .uuid = &hid_report_ref_desc_uuid.u,
                        .access_cb = gatt_svr_dsc_access_report_ref_consumer,
                        .att_flags = BLE_ATT_F_READ,
                    },
                    {
                        0, /* End of descriptors */
                    },
                },
            },
            {
                /*** Characteristic: Output Report (LED Status) - Report ID 1 */
                .uuid = &hid_data_reporting_uuid.u, // Using same UUID for different reports, distinguished by Report IDs.
                .access_cb = gatt_svr_chr_access_output_report,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP, // Read is included to allow reading current LED status if needed, but typically Output Reports are write-only from host.
                .val_handle = &output_Report_chr_val_handle,
                .descriptors = (struct ble_gatt_dsc_def[]){
                    {
                        /*** Descriptor: Report Reference for Output Report (Report ID = 1, Output) */
                        .uuid = &hid_report_ref_desc_uuid.u,
                        .access_cb = gatt_svr_dsc_access_report_ref_output,
                        .att_flags = BLE_ATT_F_READ,
                    },
                    {
                        0, /* End of descriptors */
                    },
                },
            },
            {
                /*** Characteristic: Protocol Mode Control */
                .uuid = &hid_Protocol_mode_control_uuid.u,
                .access_cb = gatt_svr_chr_access_protocol_mode,
                .flags = BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_READ,
            },
            {
                0, /* End of characteristics */
            },
        },
    },
    {
        0, /* End of services */
    },
};

/**
 * @brief Access callback for HID Information characteristic.
 *
 * @param conn_handle
 * @param attr_handle
 * @param ctxt
 * @param arg
 * @return int
 */
static int
gatt_svr_chr_access_hid_info(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt,
    void *arg)
{
    ESP_LOGI(TAG, "Characteristic read: HID Information");
    // HID Information data (HID Protocol Version, Device subclass, Boot Protocol, etc.)
    uint8_t hid_info_data[4] = { 0x11, 0x01, 0x00, 0b00000001 }; // HID 1.11, Boot Protocol, Keyboard
    return os_mbuf_append(ctxt->om, hid_info_data, sizeof(hid_info_data));
}

/**
 * @brief Access callback for Report Map characteristic.
 *
 * @param conn_handle
 * @param attr_handle
 * @param ctxt
 * @param arg
 * @return int
 */
static int
gatt_svr_chr_access_report_map(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt,
    void *arg)
{
    ESP_LOGI(TAG, "Characteristic read: Report Map");
    // Report Map data - Defines the structure of HID reports.
    // This is a simplified example, adjust according to your device requirements.
    uint8_t report_map_data[] = {
        // ----- Report ID 1: Keyboard Input Report
        0x05, 0x01, //   USAGE_PAGE (Generic Desktop)
        0x09, 0x06, //   USAGE (Keyboard)
        0xa1, 0x01, //   COLLECTION (Application)
        0x85, 0x01, //   Report ID (1) - Important!
        0x05, 0x07, //   USAGE_PAGE (Keyboard)
        0x19, 0xe0, //   USAGE_MINIMUM (Keyboard LeftControl)
        0x29, 0xe7, //   USAGE_MAXIMUM (Keyboard Right GUI)
        0x15, 0x00, //   LOGICAL_MINIMUM (0)
        0x25, 0x01, //   LOGICAL_MAXIMUM (1)
        0x75, 0x01, //   REPORT_SIZE (1)
        0x95, 0x08, //   REPORT_COUNT (8)
        0x81, 0x02, //   INPUT (Data,Var,Abs)
        0x95, 0x01, //   REPORT_COUNT (1)
        0x75, 0x08, //   REPORT_SIZE (8)
        0x81, 0x03, //   INPUT (Cnst,Var,Abs)
        0x95, 0x05, //   REPORT_COUNT (5)
        0x75, 0x01, //   REPORT_SIZE (1)
        0x05, 0x08, //   USAGE_PAGE (LEDs)
        0x19, 0x01, //   USAGE_MINIMUM (Num Lock)
        0x29, 0x05, //   USAGE_MAXIMUM (Kana)
        0x91, 0x02, //   OUTPUT (Data,Var,Abs)
        0x95, 0x01, //   REPORT_COUNT (1)
        0x75, 0x03, //   REPORT_SIZE (3)
        0x91, 0x03, //   OUTPUT (Cnst,Var,Abs)
        0x95, 0x06, //   REPORT_COUNT (6)
        0x75, 0x08, //   REPORT_SIZE (8)
        0x15, 0x00, //   LOGICAL_MINIMUM (0)
        0x25, 0x65, //   LOGICAL_MAXIMUM (101)
        0x05, 0x07, //   USAGE_PAGE (Keyboard)
        0x19, 0x00, //   USAGE_MINIMUM (Reserved (no event indicated))
        0x29, 0x65, //   USAGE_MAXIMUM (Keyboard Application)
        0x81, 0x00, //   INPUT (Data,Ary,Abs)
        0xc0,       //   END_COLLECTION)

        // ----- Report ID 2: Consumer Control Report
        0x05, 0x0C, // Usage Pg (Consumer Devices)
        0x09, 0x01, // Usage (Consumer Control)
        0xA1, 0x01, // Collection (Application)
        0x85, 0x02, // Report Id (2)
        0x09, 0x02, //   Usage (Numeric Key Pad)
        0xA1, 0x02, //   Collection (Logical)
        0x05, 0x09, //     Usage Pg (Button)
        0x19, 0x01, //     Usage Min (Button 1)
        0x29, 0x0A, //     Usage Max (Button 10)
        0x15, 0x01, //     Logical Min (1)
        0x25, 0x0A, //     Logical Max (10)
        0x75, 0x04, //     Report Size (4)
        0x95, 0x01, //     Report Count (1)
        0x81, 0x00, //     Input (Data, Ary, Abs)
        0xC0,       //   End Collection
        0x05, 0x0C, //   Usage Pg (Consumer Devices)
        0x09, 0x86, //   Usage (Channel)
        0x15, 0xFF, //   Logical Min (-1)
        0x25, 0x01, //   Logical Max (1)
        0x75, 0x02, //   Report Size (2)
        0x95, 0x01, //   Report Count (1)
        0x81, 0x46, //   Input (Data, Var, Rel, Null)
        0x09, 0xE9, //   Usage (Volume Up)
        0x09, 0xEA, //   Usage (Volume Down)
        0x15, 0x00, //   Logical Min (0)
        0x75, 0x01, //   Report Size (1)
        0x95, 0x02, //   Report Count (2)
        0x81, 0x02, //   Input (Data, Var, Abs)
        0x09, 0xE2, //   Usage (Mute)
        0x09, 0x30, //   Usage (Power)
        0x09, 0x83, //   Usage (Recall Last)
        0x09, 0x81, //   Usage (Assign Selection)
        0x09, 0xB0, //   Usage (Play)
        0x09, 0xB1, //   Usage (Pause)
        0x09, 0xB2, //   Usage (Record)
        0x09, 0xB3, //   Usage (Fast Forward)
        0x09, 0xB4, //   Usage (Rewind)
        0x09, 0xB5, //   Usage (Scan Next)
        0x09, 0xB6, //   Usage (Scan Prev)
        0x09, 0xB7, //   Usage (Stop)
        0x15, 0x01, //   Logical Min (1)
        0x25, 0x0C, //   Logical Max (12)
        0x75, 0x04, //   Report Size (4)
        0x95, 0x01, //   Report Count (1)
        0x81, 0x00, //   Input (Data, Ary, Abs)
        0x09, 0x80, //   Usage (Selection)
        0xA1, 0x02, //   Collection (Logical)
        0x05, 0x09, //     Usage Pg (Button)
        0x19, 0x01, //     Usage Min (Button 1)
        0x29, 0x03, //     Usage Max (Button 3)
        0x15, 0x01, //     Logical Min (1)
        0x25, 0x03, //     Logical Max (3)
        0x75, 0x02, //     Report Size (2)
        0x81, 0x00, //     Input (Data, Ary, Abs)
        0xC0,       //   End Collection
        0x81, 0x03, //   Input (Const, Var, Abs)
        0xC0,       // End Collection
    };
    return os_mbuf_append(ctxt->om, report_map_data, sizeof(report_map_data));
}

/**
 * @brief Access callback for Control Point characteristic.
 *        Handles write operations from the host.
 *
 * @param conn_handle
 * @param attr_handle
 * @param ctxt
 * @param arg
 * @return int
 */
static int
gatt_svr_chr_access_control_Point(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt,
    void *arg)
{
    ESP_LOGI(TAG, "Access Control Point Characteristic");
    uint8_t received_control_value;

    switch (ctxt->op)
    {
    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            ESP_LOGI(TAG, "Control Point write; conn_handle=%d attr_handle=%d", conn_handle, attr_handle);
        }
        else {
            ESP_LOGI(TAG, "Control Point write by NimBLE stack; attr_handle=%d", attr_handle);
            return BLE_ATT_ERR_UNLIKELY;
        }

        if (attr_handle == Control_Point_chr_val_handle) {
            received_control_value = ctxt->om->om_data[0];
            ESP_LOGI(TAG, "Control Point value received: 0x%02x", received_control_value);
            // Handle Control Point commands here, e.g., SET_PROTOCOL_MODE, etc.
        }
        else {
            ESP_LOGE(TAG, "Control Point write error: incorrect attribute handle.");
        }
        break;

    default:
        ESP_LOGE(TAG, "Control Point access error: unexpected operation opcode: %d", ctxt->op);
        return BLE_ATT_ERR_UNLIKELY;
    }
    return 0;
}

/**
 * @brief Access callback for Input Report characteristic (Key Press and Consumer Control).
 *        Handles read and notify operations. Write is not permitted.
 *
 * @param conn_handle
 * @param attr_handle
 * @param ctxt
 * @param arg
 * @return int
 */
static int
gatt_svr_chr_access_input_report(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt,
    void *arg)
{
    ESP_LOGI(TAG, "Access Input Report Characteristic, operation: %d", ctxt->op);

    switch (ctxt->op)
    {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        ESP_LOGI(TAG, "Input Report read by host.");
        uint8_t input_report_data[8] = { 0 }; // Return empty report for read requests
        return os_mbuf_append(ctxt->om, input_report_data, sizeof(input_report_data));

    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        ESP_LOGE(TAG, "Input Report write attempt from host - not permitted.");
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;

    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}

/**
 * @brief Access callback for Output Report characteristic (LED Status).
 *        Handles read and write operations.
 *
 * @param conn_handle
 * @param attr_handle
 * @param ctxt
 * @param arg
 * @return int
 */
static int
gatt_svr_chr_access_output_report(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt,
    void *arg)
{
    static uint8_t output_report_value = 0x00; // Store current output report value
    ESP_LOGI(TAG, "Access Output Report Characteristic, operation: %d", ctxt->op);

    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        ESP_LOGI(TAG, "Output Report read by host.");
        return os_mbuf_append(ctxt->om, &output_report_value, sizeof(output_report_value));

    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        output_report_value = ctxt->om->om_data[0];
        ESP_LOGI(TAG, "Output Report write from host, value: 0x%02x", output_report_value);
        // Update LEDs based on output_report[0] value
        break;

    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
    return 0;
}

/**
 * @brief Access callback for Protocol Mode characteristic.
 *        Handles read and write operations for protocol mode.
 *
 * @param conn_handle
 * @param attr_handle
 * @param ctxt
 * @param arg
 * @return int
 */
static int
gatt_svr_chr_access_protocol_mode(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt,
    void *arg)
{
    int rc = 0;
    ESP_LOGI(TAG, "Access Protocol Mode Characteristic, operation: %d", ctxt->op);
    switch (ctxt->op)
    {
    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        protocol_mode_Report[0] = ctxt->om->om_data[0];
        ESP_LOGI(TAG, "Protocol Mode value written: 0x%02x", protocol_mode_Report[0]);
        break;

    case BLE_GATT_ACCESS_OP_READ_CHR:
        ESP_LOGI(TAG, "Protocol Mode read.");
        rc = os_mbuf_append(ctxt->om, protocol_mode_Report, sizeof(protocol_mode_Report));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;

    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
    return 0;
}

/**
 * @brief Access callback for Report Reference Descriptor of Key Press Input Report.
 *        Report ID = 1, Report Type = Input.
 */
static int
gatt_svr_dsc_access_report_ref_key(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int rc = 0;
    uint8_t report_ref_value[] = { 0x01, 0x01 }; // Report ID = 1, Report Type = Input
    ESP_LOGI(TAG, "Read Report Reference Descriptor for Key Press Input Report.");
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_DSC) {
        rc = os_mbuf_append(ctxt->om, report_ref_value, sizeof(report_ref_value));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

/**
 * @brief Access callback for Report Reference Descriptor of Consumer Control Input Report.
 *        Report ID = 2, Report Type = Input.
 */
static int
gatt_svr_dsc_access_report_ref_consumer(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int rc = 0;
    uint8_t report_ref_value[] = { 0x02, 0x01 }; // Report ID = 2, Report Type = Input
    ESP_LOGI(TAG, "Read Report Reference Descriptor for Consumer Control Input Report.");

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_DSC) {
        rc = os_mbuf_append(ctxt->om, report_ref_value, sizeof(report_ref_value));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

/**
 * @brief Access callback for Report Reference Descriptor of Output Report.
 *        Report ID = 1, Report Type = Output.
 */
static int
gatt_svr_dsc_access_report_ref_output(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int rc = 0;
    uint8_t report_ref_value[] = { 0x01, 0x02 }; // Report ID = 1, Report Type = Output
    ESP_LOGI(TAG, "Read Report Reference Descriptor for Output Report.");
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_DSC) {
        rc = os_mbuf_append(ctxt->om, report_ref_value, sizeof(report_ref_value));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

/**
 * @brief Sends a HID Input Report notification over Bluetooth.
 *
 * @param report_data Pointer to the report data.
 * @param report_type Type of report to send (0 for Consumer, 1 for Key Press). Consider using enum for better readability.
 */
void ble_keyboard_send_input_report(uint8_t *report_data, uint8_t report_type)
{
    struct os_mbuf *om;
    int rc;
    uint16_t chr_val_handle;

    if (gatt_connection_handle == 0) {
        ESP_LOGE(TAG, "Error: No Bluetooth connection established. Cannot send input report.");
        return;
    }

    om = ble_hs_mbuf_from_flat(report_data, report_type); // Corrected size calculation
    if (om == NULL) {
        ESP_LOGE(TAG, "Failed to allocate mbuf for input report");
        return;
    }

    // Select characteristic value handle based on report_type
    if (report_type >= 4) { // Assuming 1 for Key Press
        chr_val_handle = Key_Press_chr_val_handle;
        ESP_LOGI(TAG, "Sending Key Press Input report");
    }
    else {             // Assuming 0 or other for Consumer Control
        chr_val_handle = Consumer_Press_chr_val_handle;
        ESP_LOGI(TAG, "Sending Consumer Control Input report");
    }

    rc = ble_gatts_notify_custom(gatt_connection_handle, chr_val_handle, om);

    if (rc != 0) {
        ESP_LOGE(TAG, "Error sending input report; rc=%d (NimBLE error code)", rc);
        os_mbuf_free_chain(om);
    }
}

/**
 * @brief GATT attribute registration callback.
 *        Logs service, characteristic, and descriptor registration events.
 *
 * @param ctxt
 * @param arg
 */
void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    char buf[BLE_UUID_STR_LEN];

    switch (ctxt->op)
    {
    case BLE_GATT_REGISTER_OP_SVC:
        ESP_LOGD(TAG, "Registered service %s with handle=%d",
            ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf), ctxt->svc.handle);
        break;

    case BLE_GATT_REGISTER_OP_CHR:
        ESP_LOGD(TAG, "Registered characteristic %s with def_handle=%d val_handle=%d",
            ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf), ctxt->chr.def_handle, ctxt->chr.val_handle);
        break;

    case BLE_GATT_REGISTER_OP_DSC:
        ESP_LOGD(TAG, "Registered descriptor %s with handle=%d",
            ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf), ctxt->dsc.handle);
        break;

    default:
        assert(0);
        break;
    }
}

/**
 * @brief Initializes the GATT server with HID service.
 *
 * @return esp_err_t ESP_OK on success, otherwise error code.
 */
esp_err_t gatt_svc_init(void)
{
    esp_err_t rc = 0;

    ble_svc_gatt_init(); // Initialize generic GATT service

    rc = ble_gatts_count_cfg(gatt_svr_svcs); // Count and configure GATT services.
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to count GATT services, rc=%d", rc);
        return rc;
    }

    rc = ble_gatts_add_svcs(gatt_svr_svcs); // Add services to the GATT server.
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to add GATT services, rc=%d", rc);
        return rc;
    }

    ESP_LOGI(TAG, "GATT HID service initialized.");
    return ESP_OK;
}
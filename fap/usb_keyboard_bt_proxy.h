#pragma once

#include <bt/bt_service/bt.h>
#include <core/log.h>
#include <furi_hal_bt.h>
#include <furi_hal_usb.h>
#include <furi_hal_usb_hid.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>
#include <targets/f7/ble_glue/furi_ble/gatt.h>
#include <targets/f7/ble_glue/furi_ble/event_dispatcher.h>

#define TAG "UsbKeyboardBtProxy"
#define HID_BT_KEYS_STORAGE_NAME ".UsbKbBtP.keys"
#define DEVICE_NAME_PREFIX "UsbKbBtP"

#define CONNECTION_INTERVAL_MIN (0x06)
#define CONNECTION_INTERVAL_MAX (0x24)
#define SLAVE_LATENCY 0
#define SUPERVISOR_TIMEOUT 500 //500 * 10 ms, not sure whether needed
#define ACI_GATT_ATTRIBUTE_MODIFIED_VSEVT_CODE 0x0C01U
#define CHARACTERISTIC_NAME "Keyboard Event"
#define EVENT_PRESSED 1
#define MAC_XOR 0x06af

typedef enum {
    ViewIdMainMenu,
    ViewIdStartPage
} ViewId;

typedef struct {
    View* text_view;
} StartPage;

typedef struct {
    uint16_t svc_handle;
    BleGattCharacteristicInstance char_instance;
    GapSvcEventHandler* event_handler;
} BleService;

typedef struct {
    FuriHalBleProfileBase base;
    BleService* svc;
} BleProfile;

typedef struct {
    BleGattCharacteristicInstance* char_instance;
    BleProfile* ble_profile;
    FuriHalBleProfileBase* base;
    Bt* bt;
} Ble;

typedef struct {
    const char* title;
    const char* text;
} TextModel;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    StartPage* start_page;
    Ble* ble;
    NotificationApp* notifications;
} UsbKeyboardBtProxy;

typedef struct {
    const char* device_name_prefix;
    uint16_t mac_xor;
} UsbKeyboardBtProxyProfileParams;

typedef PACKED(struct) _hci_uart_pckt
{
  uint8_t type;
  uint8_t data[1];
} hci_uart_pckt;

typedef PACKED(struct) _hci_event_pckt
{
  uint8_t         evt;
  uint8_t         plen;
  uint8_t         data[1];
} hci_event_pckt;

typedef PACKED(struct) _evt_le_meta_event
{
  uint8_t         subevent;
  uint8_t         data[1];
} evt_le_meta_event;

typedef PACKED(struct) _evt_blecore_aci
{
  uint16_t ecode;
  uint8_t  data[1];
} evt_blecore_aci;

static const Service_UUID_t service_uuid = {
    .Service_UUID_128 = {
        0x77,
        0xb6,
        0x7f,
        0xce,
        0x96,
        0x02,
        0x4f,
        0xe6,
        0xad,
        0x12,
        0xd4,
        0x62,
        0xad,
        0xf8,
        0x67,
        0x6f,
    }
};

// Holds pressed/released (uint8) + scancode (uint8) + modifiers (uint8)
static const char char_data[3];

BleGattCharacteristicParams char_descriptor = {
    .name = CHARACTERISTIC_NAME,
    .uuid = {
        .Char_UUID_128 = {
            0x91,
            0x56,
            0x6c,
            0x96,
            0xb9,
            0x7e,
            0x48,
            0x86,
            0x9c,
            0x80,
            0x93,
            0xee,
            0xc5,
            0x16,
            0x06,
            0xca,
        }
    },
    .data_prop_type = FlipperGattCharacteristicDataFixed,
    .uuid_type = UUID_TYPE_128,
    .data.callback.fn = NULL,
    .data.callback.context = NULL,
    .data.fixed.length = sizeof(char_data),
    .data.fixed.ptr = (const uint8_t*)&char_data,
    .char_properties = CHAR_PROP_WRITE_WITHOUT_RESP,
    .security_permissions = ATTR_PERMISSION_AUTHEN_WRITE,
    .gatt_evt_mask = GATT_NOTIFY_ATTRIBUTE_WRITE,
    .is_variable = CHAR_VALUE_LEN_CONSTANT
};

static GapConfig template_config = {
    .adv_service_uuid = 0x3080, //AN ADVERTISING SERVICE UUID FROM STANDARD BLE
    .appearance_char = 0x8600, //AN APPERANCE CHARACTERISTIC FROM STANDARD BLE
    .bonding_mode = true,
    .pairing_method = GapPairingPinCodeVerifyYesNo,
    .conn_param = {
        .conn_int_min = CONNECTION_INTERVAL_MIN,
        .conn_int_max = CONNECTION_INTERVAL_MAX,
        .slave_latency = SLAVE_LATENCY,
        .supervisor_timeout = SUPERVISOR_TIMEOUT
    }
};

static UsbKeyboardBtProxyProfileParams ble_profile_params = {
    .device_name_prefix = DEVICE_NAME_PREFIX,
    .mac_xor = MAC_XOR
};

static FuriHalBleProfileBase* ble_profile_start(FuriHalBleProfileParams profile_params);
static void ble_profile_stop(FuriHalBleProfileBase* profile);
static void ble_profile_get_config(GapConfig* config, FuriHalBleProfileParams profile_params);

const FuriHalBleProfileTemplate profile_callbacks = {
    .start = ble_profile_start,
    .stop = ble_profile_stop,
    .get_gap_config = ble_profile_get_config,
};
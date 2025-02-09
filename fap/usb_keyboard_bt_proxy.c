#include "usb_keyboard_bt_proxy.h"

static char show_text[7];
static UsbKeyboardBtProxy* g_app;
static char* g_device_name;

static BleEventAckStatus ble_svc_event_handler(void* event, void* context) {
    BleEventAckStatus ret = BleEventNotAck;
    BleService* svc = (BleService*)context;
    hci_event_pckt* event_pckt = (hci_event_pckt*)(((hci_uart_pckt*)event)->data);
    evt_blecore_aci* blecore_evt = (evt_blecore_aci*)event_pckt->data;
    aci_gatt_attribute_modified_event_rp0* attribute_modified;

    if(event_pckt->evt == HCI_VENDOR_SPECIFIC_DEBUG_EVT_CODE) {
        if(blecore_evt->ecode == ACI_GATT_ATTRIBUTE_MODIFIED_VSEVT_CODE) {
            attribute_modified = (aci_gatt_attribute_modified_event_rp0*)blecore_evt->data;
            if(attribute_modified->Attr_Handle == (svc->char_instance.handle + 1)) {
                const bool pressed = attribute_modified->Attr_Data[0] == EVENT_PRESSED;
                const uint16_t scancode = (uint16_t)attribute_modified->Attr_Data[1];
                const uint16_t modifier = ((uint16_t)attribute_modified->Attr_Data[2]) << 8;
                // Handling ping. Not sure if it is really needed though.
                if(!pressed && !scancode && !modifier) {
                    return ret;
                }
                ret = BleEventAckFlowEnable;
                snprintf(show_text, sizeof(show_text), "0x%04x", scancode | modifier);
                with_view_model(
                    g_app->start_page->text_view,
                    TextModel * model,
                    {
                        model->title = pressed ? "Pressed" : "Released";
                        model->text = show_text;
                    },
                    true);
                if(pressed) {
                    furi_hal_hid_kb_press(scancode | modifier);
                } else {
                    furi_hal_hid_kb_release(scancode | modifier);
                }
            }
        }
    }
    return ret;
}

BleService* ble_svc_start(void) {
    BleService* svc = malloc(sizeof(BleService));

    svc->event_handler = ble_event_dispatcher_register_svc_handler(ble_svc_event_handler, svc);

    FURI_LOG_I(TAG, "Service started");
    if(!ble_gatt_service_add(UUID_TYPE_128, &service_uuid, PRIMARY_SERVICE, 20, &svc->svc_handle)) {
        free(svc);
        return NULL;
    }
    FURI_LOG_I(TAG, "Service added");
    FURI_LOG_I(TAG, "Service handle: %d", svc->svc_handle);

    ble_gatt_characteristic_init(svc->svc_handle, &char_descriptor, &svc->char_instance);
    FURI_LOG_I(TAG, "Characteristic added");

    return svc;
}

static void ble_svc_stop(BleService* svc) {
    furi_check(svc);

    ble_event_dispatcher_unregister_svc_handler(svc->event_handler);

    ble_gatt_characteristic_delete(svc->svc_handle, &svc->char_instance);
    FURI_LOG_I(TAG, "Characteristic deleted");

    ble_gatt_service_delete(svc->svc_handle);
    FURI_LOG_I(TAG, "Service deleted");

    free(svc);
}

static FuriHalBleProfileBase* ble_profile_start(FuriHalBleProfileParams profile_params) {
    UNUSED(profile_params);

    FURI_LOG_I(TAG, "Profile started");

    BleProfile* profile = malloc(sizeof(BleProfile));

    profile->svc = ble_svc_start();

    return &profile->base;
}

static void ble_profile_stop(FuriHalBleProfileBase* profile) {
    UNUSED(profile);

    FURI_LOG_I(TAG, "Profile stopped");

    BleProfile* ble_profile = (BleProfile*)profile;

    ble_svc_stop(ble_profile->svc);
}

static void ble_profile_get_config(GapConfig* config, FuriHalBleProfileParams profile_params) {
    UsbKeyboardBtProxyProfileParams* ble_app_profile_params =
        (UsbKeyboardBtProxyProfileParams*)profile_params;

    furi_check(config);

    memcpy(config, &template_config, sizeof(GapConfig));
    // Set mac address
    memcpy(config->mac_address, furi_hal_version_get_ble_mac(), sizeof(config->mac_address));

    // Change MAC address
    config->mac_address[0] ^= ble_app_profile_params->mac_xor;
    config->mac_address[1] ^= ble_app_profile_params->mac_xor >> 8;
    config->mac_address[2] ^= ble_app_profile_params->mac_xor >> 16;

    // Set advertise name
    memset(config->adv_name, 0, sizeof(config->adv_name));
    FuriString* name = furi_string_alloc_set(furi_hal_version_get_ble_local_device_name_ptr());

    furi_string_replace_str(name, "Flipper", ble_app_profile_params->device_name_prefix);
    if(furi_string_size(name) >= sizeof(config->adv_name)) {
        furi_string_left(name, sizeof(config->adv_name) - 1);
    }
    memcpy(config->adv_name, furi_string_get_cstr(name), furi_string_size(name));
    furi_string_free(name);

    g_device_name = (char*)malloc(sizeof(config->adv_name));
    memcpy(g_device_name, config->adv_name, sizeof(config->adv_name));

    FURI_LOG_I(TAG, "Got profile config");
}

static void bt_connection_status_changed_callback(BtStatus status, void* context) {
    UNUSED(context);

    const bool connected = (status == BtStatusConnected);
    notification_internal_message(
        g_app->notifications, connected ? &sequence_set_blue_255 : &sequence_reset_blue);
    with_view_model(
        g_app->start_page->text_view,
        TextModel * model,
        {
            model->title = connected ? "Connected" : "Disconnected";
            model->text = NULL;
        },
        true);
}

bool start_custom_ble_gatt_svc(UsbKeyboardBtProxy* app) {
    UNUSED(profile_callbacks);
    if(furi_hal_bt_is_active()) {
        FURI_LOG_I(TAG, "BT is working...");

        if(app->ble->bt) {
            FURI_LOG_I(TAG, "BT not initialized.");
            app->ble->bt = furi_record_open(RECORD_BT);
        }

        FURI_LOG_I(TAG, "BT disconnecting...");
        bt_disconnect(app->ble->bt);

        furi_delay_ms(200);

        // Starting a custom ble profile for service creation
        FURI_LOG_I(TAG, "BT profile starting...");
        app->ble->base =
            bt_profile_start(app->ble->bt, &profile_callbacks, (void*)&ble_profile_params);
        FURI_LOG_I(TAG, "BT profile started");

        if(!app->ble->base) {
            FURI_LOG_E(TAG, "Failed to start the app");
            return false;
        }

        app->ble->base->config = &profile_callbacks;

        FURI_LOG_I(TAG, "BT profile start advertising...");
        furi_hal_bt_start_advertising();
        bt_set_status_changed_callback(
            g_app->ble->bt, bt_connection_status_changed_callback, g_app);
    } else {
        return false;
    }

    return true;
}

static void text_view_draw_callback(Canvas* canvas, void* context) {
    furi_check(context);
    TextModel* model = context;
    const char* title = model->title;
    const char* text = model->text;

    if(!title && !text && !g_device_name) {
        return;
    }

    canvas_clear(canvas);
    if(g_device_name) {
        canvas_draw_str_aligned(canvas, 64, 10, AlignCenter, AlignBottom, g_device_name);
    }
    if(title) {
        canvas_draw_str_aligned(canvas, 64, text ? 27 : 32, AlignCenter, AlignBottom, title);
    }
    if(text) {
        canvas_draw_str_aligned(canvas, 64, 47, AlignCenter, AlignBottom, text);
    }
}

static void submenu_callback(void* context, uint32_t index) {
    furi_check(context);
    UsbKeyboardBtProxy* app = context;
    UNUSED(index);

    if(start_custom_ble_gatt_svc(app)) {
        with_view_model(
            app->start_page->text_view,
            TextModel * model,
            { model->title = "Service started..."; },
            true);

    } else {
        with_view_model(
            app->start_page->text_view,
            TextModel * model,
            {
                model->title = "Service failed to start!";
                model->text = "Make sure Bluetooth is ON.";
            },
            true);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, ViewIdStartPage);
}

bool view_dispatcher_navigation_callback_event(void* context) {
    UNUSED(context);
    FURI_LOG_I(TAG, "Navigation callback");
    return false;
}

int32_t usb_keyboard_bt_proxy_app(void* p) {
    UNUSED(p);
    FURI_LOG_I(TAG, "Start");

    FuriHalUsbInterface* usb_mode_prev = furi_hal_usb_get_config();
    furi_hal_usb_unlock();
    furi_check(furi_hal_usb_set_config(&usb_hid, NULL) == true);
    furi_hal_hid_kb_release_all();

    g_app = (UsbKeyboardBtProxy*)malloc(sizeof(UsbKeyboardBtProxy));
    g_app->notifications = furi_record_open(RECORD_NOTIFICATION);
    g_app->gui = furi_record_open(RECORD_GUI);
    g_app->view_dispatcher = view_dispatcher_alloc();
    g_app->ble = malloc(sizeof(UsbKeyboardBtProxy));
    g_app->ble->bt = furi_record_open(RECORD_BT);

    bt_keys_storage_set_storage_path(g_app->ble->bt, APP_DATA_PATH(HID_BT_KEYS_STORAGE_NAME));

    // Create a submenu
    g_app->submenu = submenu_alloc();
    submenu_add_item(g_app->submenu, "Start", 0, submenu_callback, g_app);

    // Add submenu to ViewDispatcher
    view_dispatcher_add_view(
        g_app->view_dispatcher, ViewIdMainMenu, submenu_get_view(g_app->submenu));

    // Initializing
    g_app->start_page = malloc(sizeof(StartPage));

    // Create a view for displaying text
    g_app->start_page->text_view = view_alloc();

    // Attach views to ViewDispatcher
    view_dispatcher_add_view(
        g_app->view_dispatcher, ViewIdStartPage, g_app->start_page->text_view);
    view_dispatcher_set_event_callback_context(g_app->view_dispatcher, g_app);
    view_dispatcher_set_navigation_event_callback(
        g_app->view_dispatcher, view_dispatcher_navigation_callback_event);
    // Set up text view
    view_set_context(g_app->start_page->text_view, g_app);
    view_allocate_model(g_app->start_page->text_view, ViewModelTypeLockFree, sizeof(TextModel));
    view_set_draw_callback(g_app->start_page->text_view, text_view_draw_callback);

    view_dispatcher_switch_to_view(g_app->view_dispatcher, ViewIdMainMenu);

    FURI_LOG_I(TAG, "Running view dispatcher");

    view_dispatcher_attach_to_gui(
        g_app->view_dispatcher, g_app->gui, ViewDispatcherTypeFullscreen);

    // Run the ViewDispatcher => application loop (on exit the loop ends, and
    // the below cleanup code is executed)
    view_dispatcher_run(g_app->view_dispatcher);

    // Cleanup

    // BLE
    if(g_app->ble->bt) {
        FURI_LOG_I(TAG, "Disconnecting from BT");
        bt_set_status_changed_callback(g_app->ble->bt, NULL, NULL);
        bt_disconnect(g_app->ble->bt);

        // Wait 2nd core to update nvm storage
        furi_delay_ms(200);

        FURI_LOG_I(TAG, "Restoring default profile");
        bt_keys_storage_set_default_path(g_app->ble->bt);
        bt_profile_restore_default(g_app->ble->bt);

        FURI_LOG_I(TAG, "Closing BT record");
        furi_record_close(RECORD_BT);
    }

    // GUI
    view_free_model(g_app->start_page->text_view);
    view_dispatcher_remove_view(g_app->view_dispatcher, ViewIdMainMenu);
    view_dispatcher_remove_view(g_app->view_dispatcher, ViewIdStartPage);
    view_dispatcher_free(g_app->view_dispatcher);
    FURI_LOG_I(TAG, "View dispatcher freed");

    notification_internal_message(g_app->notifications, &sequence_reset_blue);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);
    FURI_LOG_I(TAG, "GUI record closed");

    free(g_device_name);

    // Free resources of the app (may not be exhaustive)
    free(g_app->ble->base);
    free(g_app->ble->ble_profile);
    free(g_app->ble->char_instance);
    free(g_app->ble);

    view_free_model(g_app->start_page->text_view);
    view_free(g_app->start_page->text_view);
    free(g_app->start_page);

    submenu_free(g_app->submenu);

    furi_hal_usb_set_config(usb_mode_prev, NULL);

    free(g_app);

    return 0;
}

#include <furi.h>
#include <furi_hal.h>

#include <gui/gui.h>
#include <gui/view_port.h>

#include <notification/notification.h>
#include <notification/notification_messages.h>

#define DEVICE_ADDRESS (0xA4)

typedef struct {
    uint32_t distance;
    bool ready;
} State;

static void render_callback(Canvas* canvas, void* ctx) {
    State* state = ctx;
    char buffer[64];

    canvas_set_font(canvas, FontPrimary);
    if(state->ready) {
        snprintf(buffer, sizeof(buffer), "Distance: %lu mm", state->distance);
    } else {
        snprintf(buffer, sizeof(buffer), "Device is not ready");
    }

    canvas_draw_str(canvas, 0, 24, buffer);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 10, 63, "[back] - reset, hold to exit");
}

static void input_callback(InputEvent* input_event, void* ctx) {
    FuriMessageQueue* event_queue = ctx;
    furi_message_queue_put(event_queue, input_event, FuriWaitForever);
}

int32_t mtof171000c0_app(void* p) {
    UNUSED(p);

    FuriMessageQueue* event_queue = furi_message_queue_alloc(32, sizeof(InputEvent));
    State state = {0};
    ViewPort* view_port = view_port_alloc();

    view_port_draw_callback_set(view_port, render_callback, &state);
    view_port_input_callback_set(view_port, input_callback, event_queue);

    // Open GUI and register view_port
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    NotificationApp* notification = furi_record_open(RECORD_NOTIFICATION);
    notification_message_block(notification, &sequence_display_backlight_enforce_on);

    InputEvent event;
    while(true) {
        if(furi_message_queue_get(event_queue, &event, 100) != FuriStatusOk) {
            furi_hal_gpio_write(&gpio_ext_pc3, 1);
            furi_hal_gpio_init_simple(&gpio_ext_pc3, GpioModeOutputOpenDrain);

            furi_hal_i2c_acquire(&furi_hal_i2c_handle_external);
            state.ready =
                furi_hal_i2c_is_device_ready(&furi_hal_i2c_handle_external, DEVICE_ADDRESS, 2);
            if(state.ready) {
                uint8_t data[2] = {0xD3, 0};

                furi_hal_gpio_write(&gpio_ext_pc3, 0);
                furi_hal_i2c_tx(&furi_hal_i2c_handle_external, DEVICE_ADDRESS, data, 1, 10);
                furi_hal_i2c_rx(
                    &furi_hal_i2c_handle_external, DEVICE_ADDRESS, data, sizeof(data), 10);
                furi_hal_gpio_write(&gpio_ext_pc3, 1);

                state.distance = ((uint32_t)data[0] << 8) | data[1];
            }
            furi_hal_i2c_release(&furi_hal_i2c_handle_external);

            furi_hal_gpio_init_simple(&gpio_ext_pc3, GpioModeAnalog);
            view_port_update(view_port);
            continue;
        }

        if(event.key == InputKeyBack) {
            break;
        }

        view_port_update(view_port);
    }

    notification_message(notification, &sequence_display_backlight_enforce_auto);
    furi_record_close(RECORD_NOTIFICATION);

    // remove & free all stuff created by app
    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_message_queue_free(event_queue);

    furi_record_close(RECORD_GUI);

    return 0;
}

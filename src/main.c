#include <wayland-client.h>
#include <dbus/dbus.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "cm.h"
#include "dbus.h"
#include "outputs.h"

#include "color-management-v1-client-protocol.h"
#include "wlr-gamma-control-unstable-v1-client-protocol.h"
#include "wlr-output-management-unstable-v1-client-protocol.h"

static output_info outputs[16];
static int output_count = 0;
static struct zwlr_output_manager_v1 *output_manager = NULL;
static struct zwlr_gamma_control_manager_v1 *gamma_manager = NULL;
static struct wp_color_manager_v1 *color_manager = NULL;

// --- Gamma Control ---

static void gamma_control_gamma_size(void *data, struct zwlr_gamma_control_v1 *control, uint32_t size) {
    output_info *info = data;
    info->gamma_size = size;
    printf("  %s: gamma ramp size = %u\n", info->con_name, size);

    if (!info->cm_output && color_manager) {
        cm_init_output(color_manager, info);
    }
}

static void gamma_control_failed(void *data, struct zwlr_gamma_control_v1 *control) {
    output_info *info = data;
    fprintf(stderr, "  %s: gamma control failed (already running?)\n", info->con_name);
    zwlr_gamma_control_v1_destroy(control);
    info->gamma_control = NULL;
}

static const struct zwlr_gamma_control_v1_listener gamma_control_listener = {
    .gamma_size = gamma_control_gamma_size,
    .failed     = gamma_control_failed,
};

// --- Output ---

static void output_name(void *data, struct wl_output *wl_output, const char *name) {
    output_info *info = data;
    strncpy(info->con_name, name, 255);
}

static void output_description(void *data, struct wl_output *wl_output, const char *description) { }
static void output_geometry(void *data, struct wl_output *wl_output,
    int32_t x, int32_t y, int32_t w, int32_t h,
    int32_t subpixel, const char *make, const char *model, int32_t transform) { }
static void output_mode(void *data, struct wl_output *wl_output, uint32_t flags, int32_t width, int32_t height, int32_t refresh) { }
static void output_done(void *data, struct wl_output *wl_output) {
    output_info *info = data;

    if (!info->gamma_control && gamma_manager) {
        info->gamma_control = zwlr_gamma_control_manager_v1_get_gamma_control(gamma_manager, info->output);
        zwlr_gamma_control_v1_add_listener(info->gamma_control, &gamma_control_listener, info);
    }
}
static void output_scale(void *data, struct wl_output *wl_output, int32_t factor) { }

static const struct wl_output_listener output_listener = {
    output_geometry, output_mode, output_done, output_scale, output_name, output_description
};

// Hook up to display events
static void registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    if (strcmp(interface, wl_output_interface.name) == 0) {
        output_info *info = NULL;
        for (int i = 0; i < output_count; i++) {
            // Try to reuse struct from deactivated outputs
            if (!outputs[i].active) {
                info = &outputs[i];
                break;
            }
        }
        if (!info) {
            if (output_count >= 16) {
                fprintf(stderr, "Max output count reached\n");
                return;
            }
            info = &outputs[output_count++];
        }

        memset(info, 0, sizeof(output_info));
        info->active = 1;
        info->global_name = name;

        if (version < 2) {
            fprintf(stderr, "Requires %s protocol version 2. Provided %d.\n", interface, version);
            return;
        }
        if (version < 4) {
            fprintf(stderr, "Connector name requires %s protocol version 4. Provided %d.\n", interface, version);
            info->output = wl_registry_bind(registry, name, &wl_output_interface, 2);
        }
        else {
            info->output = wl_registry_bind(registry, name, &wl_output_interface, 4);
        }
        
        wl_output_add_listener(info->output, &output_listener, info);
    }
    else if (strcmp(interface, zwlr_output_manager_v1_interface.name) == 0) {
        output_manager = wl_registry_bind(registry, name, &zwlr_output_manager_v1_interface, 4);
        zwlr_output_manager_v1_add_listener(output_manager, &manager_listener, NULL);
    }
    else if (strcmp(interface, wp_color_manager_v1_interface.name) == 0) {
        color_manager = wl_registry_bind(registry, name, &wp_color_manager_v1_interface, 1); // version 1 is fine since we only care about PQ EOTF or not
        wp_color_manager_v1_add_listener(color_manager, &color_manager_listener, NULL);
    }
    else if (strcmp(interface, zwlr_gamma_control_manager_v1_interface.name) == 0) {
        gamma_manager = wl_registry_bind(registry, name, &zwlr_gamma_control_manager_v1_interface, 1);
    }
}

// Tear down on output loss
static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    for (int i = 0; i < output_count; i++) {
        if (outputs[i].active && outputs[i].global_name == name) {
            output_info *info = &outputs[i];
            if (info->gamma_control) zwlr_gamma_control_v1_destroy(info->gamma_control);
            if (info->cm_output) wp_color_management_output_v1_destroy(info->cm_output);
            if (info->image_desc) wp_image_description_v1_destroy(info->image_desc);
            if (info->output) {
                if (wl_proxy_get_version((struct wl_proxy *)info->output) >= 3) {
                    wl_output_release(info->output);
                } else {
                    wl_output_destroy(info->output);
                }
            }
            info->active = 0;
            break;
        }
    }
}

// Intro to Wayland from https://drewdevault.com/blog/Introduction-to-Wayland/
int main(void) {
    // Read from config file
    config_sz = config_read(&cfg);
    if(config_sz < 0) {
        fprintf(stderr, "Failed to read from config file.\n");
        return 1;
    }

    // Establish a connection to the Wayland server
    struct wl_display *display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "Cannot connect to Wayland\n");
        return 1;
    }

    // Enumerates the globals available on the server
    struct wl_registry *registry = wl_display_get_registry(display);

    // Listen to events emitted by the registry
    struct wl_registry_listener registry_listener = {
        .global        = registry_global,
        .global_remove = registry_global_remove,
    };
    wl_registry_add_listener(registry, &registry_listener, NULL); // void *data unused

    wl_display_roundtrip(display);   // collect globals
    wl_display_roundtrip(display);  // collect output events (may call global_remove when the server destroys globals)

    printf("Found %d display(s):\n", output_count);
    for (int i = 0; i < output_count; i++) {
        output_info *o = &outputs[i];
        if (!o->active) continue;
        head_state *hs = get_head_state(o->con_name);
        printf("  Display %d (%s): %s %s\n",
               i, o->con_name, hs ? hs->make : "unknown", hs ? hs->model : "unknown");
    }
    printf("------------------------------------------------------------------------------\n");

    if (!output_manager) {
        fprintf(stderr, "Compositor does not support zwlr_output_manager_v1\n");
        wl_display_disconnect(display);
        return 1;
    }

    if (!color_manager) {
        fprintf(stderr, "Compositor does not support wp_color_management_v1\n");
        wl_display_disconnect(display);
        return 1;
    }

    if (!gamma_manager) {
        fprintf(stderr, "Compositor does not support zwlr_gamma_control_manager_v1\n");
        wl_display_disconnect(display);
        return 1;
    }

    DBusConnection *conn = setup_dbus();

    int running = 1;

    // Poll both Wayland and D-Bus events
    while (running) {
        if (wl_display_dispatch_pending(display) == -1) break;

        // Drain buffered D-Bus messages
        while (dbus_connection_get_dispatch_status(conn) == DBUS_DISPATCH_DATA_REMAINS)
            dbus_connection_dispatch(conn);

        while (wl_display_prepare_read(display) != 0)
            wl_display_dispatch_pending(display);

        while (wl_display_flush(display) == -1) {
            if (errno != EAGAIN) {
                wl_display_cancel_read(display);
                running = 0;
                break;
            }
            struct pollfd flush_fd = { wl_display_get_fd(display), POLLOUT, 0 };
            poll(&flush_fd, 1, -1);
        }

        if (!running) break;

        int wayland_fd = wl_display_get_fd(display);
        int dbus_fd = -1;
        dbus_connection_get_unix_fd(conn, &dbus_fd);

        struct pollfd fds[2] = {
            { .fd = wayland_fd, .events = POLLIN },
            { .fd = dbus_fd,    .events = POLLIN },
        };

        poll(fds, 2, -1);

        if (fds[0].revents & POLLIN) {
            wl_display_read_events(display);
        } else {
            wl_display_cancel_read(display);
        }

        if (fds[1].revents & POLLIN)
            dbus_connection_read_write_dispatch(conn, 0);
    }

    config_free(cfg);
    wl_display_disconnect(display);
    return 0;
}

void refresh_all_outputs(void) {
    for (int i = 0; i < output_count; i++) {
        output_info *o = &outputs[i];
        if (!o->active) continue;
        
        // Wait until image_desc is loaded to determine SDR/HDR
        if (!o->image_desc) continue;
        
        if (o->is_hdr) {
            apply_gamma_ramp(o);
        } else {
            apply_blue_light_filter_sdr(o);
        }
    }
}
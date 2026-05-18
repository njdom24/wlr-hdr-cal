#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "outputs.h"
#include "cm.h"
#include "wlr-output-management-unstable-v1-client-protocol.h"

int output_count = 0;
output_info outputs[16];

static head_state* heads[16];
static int head_count = 0;

static void head_name(void *data, struct zwlr_output_head_v1 *head, const char *name) {
    head_state *hs = data;

    printf("[+] Output connected: %s\n", name);
    hs->name = strdup(name);
}

static void head_description(void *data, struct zwlr_output_head_v1 *head, const char *description) {}
static void head_physical_size(void *data, struct zwlr_output_head_v1 *head, int32_t w, int32_t h) {}
static void head_mode(void *data, struct zwlr_output_head_v1 *head, struct zwlr_output_mode_v1 *mode) {}
static void head_enabled(void *data, struct zwlr_output_head_v1 *head, int32_t enabled) {
    head_state *hs = data;
    hs->enabled = enabled;
    printf("Enabled: %d\n", enabled);
}
static void head_current_mode(void *data, struct zwlr_output_head_v1 *head, struct zwlr_output_mode_v1 *mode) {}
static void head_position(void *data, struct zwlr_output_head_v1 *head, int32_t x, int32_t y) {}
static void head_transform(void *data, struct zwlr_output_head_v1 *head, int32_t transform) {}
static void head_scale(void *data, struct zwlr_output_head_v1 *head, wl_fixed_t scale) {}
static void head_make(void *data, struct zwlr_output_head_v1 *head, const char *make) {
    head_state *hs = data;
    hs->make = strdup(make);
}
static void head_model(void *data, struct zwlr_output_head_v1 *head, const char *model) {
    head_state *hs = data;
    hs->model = strdup(model);
}
static void head_serial_number(void *data, struct zwlr_output_head_v1 *head, const char *serial) {}
static void head_adaptive_sync(void *data, struct zwlr_output_head_v1 *head, uint32_t state) {}

static void head_finished(void *data, struct zwlr_output_head_v1 *head) {
    head_state *hs = data;
    printf("[-] Output disconnected: %s\n", hs->name ? hs->name : "(unknown)");
    if (hs->index >= 0 && hs->index < 16) {
        heads[hs->index] = NULL;
    }
    free(hs->name);
    free(hs->make);
    free(hs->model);
    free(hs);
    zwlr_output_head_v1_destroy(head);
}

static const struct zwlr_output_head_v1_listener head_listener = {
    .name          = head_name,
    .description   = head_description,
    .physical_size = head_physical_size,
    .mode          = head_mode,
    .enabled       = head_enabled,
    .current_mode  = head_current_mode,
    .position      = head_position,
    .transform     = head_transform,
    .scale         = head_scale,
    .make          = head_make,
    .model         = head_model,
    .serial_number = head_serial_number,
    .finished      = head_finished,
    .adaptive_sync = head_adaptive_sync,
};

// --- Manager listeners ---

static void manager_head(void *data, struct zwlr_output_manager_v1 *mgr, struct zwlr_output_head_v1 *head) {
    head_state *hs = calloc(1, sizeof(*hs));
    hs->index = -1;
    for (int i = 0; i < head_count; i++) {
        if (heads[i] == NULL) {
            hs->index = i;
            heads[i] = hs;
            break;
        }
    }
    if (hs->index == -1) {
        if (head_count >= 16) {
            fprintf(stderr, "Max head count reached\n");
            free(hs);
            return;
        }
        hs->index = head_count;
        heads[head_count++] = hs;
    }
    zwlr_output_head_v1_add_listener(head, &head_listener, hs);
}

static void manager_done(void *data, struct zwlr_output_manager_v1 *mgr, uint32_t serial) {
    // All head events have been delivered
}

static void manager_finished(void *data, struct zwlr_output_manager_v1 *mgr) {
    fprintf(stderr, "manager_finished: Compositor shutting down?\n");
    for(int i = 0; i < head_count; i++) {
        free(heads[i]);
    }
}

const struct zwlr_output_manager_v1_listener manager_listener = {
    .head     = manager_head,
    .done     = manager_done,
    .finished = manager_finished,
};

head_state* get_head_state(char *name) {
    for(int i = 0; i < head_count; i++) {
        head_state *hs = heads[i];
        if(hs && hs->name && strcmp(name, hs->name) == 0) {
            return hs;
        }
    }

    return NULL;
}

void refresh_all_outputs(void) {
    for (int i = 0; i < output_count; i++) {
        output_info *o = &outputs[i];
        if (!o->active) continue;
        if (!o->image_desc) continue;
        
        if (o->is_hdr) {
            apply_gamma_ramp(o);
        } else {
            apply_blue_light_filter_sdr(o);
        }
    }
}
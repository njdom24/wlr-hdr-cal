#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <tomlc17.h>

#include <string.h>

double toml_to_double(toml_datum_t d) {
    if (d.type == TOML_FP64)  return d.u.fp64;
    if (d.type == TOML_INT64) return (double)d.u.int64;
    return 0.0;
}

int config_read(output_config **out) {
    char config_path[4096];
    const char *home = getenv("HOME");
    if (!home) {
        fprintf(stderr, "HOME environment variable not set\n");
        return -1;
    }
    snprintf(config_path, sizeof(config_path), "%s/.config/wlr-hdr-cal/config", home);

    toml_result_t result = toml_parse_file_ex(config_path);
    if (!result.ok) {
        fprintf(stderr, "config_read: Parse error: %s\n", result.errmsg);
        return -1;
    }

    // "monitors" is an array of tables
    toml_datum_t monitors = toml_get(result.toptab, "monitors");
    if (monitors.type != TOML_ARRAY) {
        fprintf(stderr, "config_read: 'monitors' must be an array\n");
        return -1;
    }

    printf("Found %d monitor(s)\n\n", monitors.u.arr.size);
    output_config *outputs = malloc(sizeof(*outputs) * monitors.u.arr.size);
    *out = outputs;

    for (int i = 0; i < monitors.u.arr.size; i++) {
        toml_datum_t mon = monitors.u.arr.elem[i];
        if (mon.type != TOML_TABLE) {
            fprintf(stderr, "config_read: monitors[%d] is not a table\n", i);
            continue;
        }

        printf("monitors[%d]:\n", i);

        // name
        toml_datum_t name = toml_get(mon, "name");
        if (name.type == TOML_STRING)
            printf("  name       = \"%s\"\n", name.u.s);

        // multiplier (optional, default 1.0)
        toml_datum_t mult = toml_get(mon, "multiplier");
        if (mult.type == TOML_FP64)
            printf("  multiplier = %g\n", mult.u.fp64);
        else
            printf("  multiplier = 1.0 (default)\n");

        strcpy(outputs[i].name, name.u.s);
        outputs[i].multiplier = mult.u.fp64;

        // values = array of [lo, hi] pairs
        toml_datum_t values = toml_get(mon, "values");
        if (values.type == TOML_ARRAY) {
            printf("  values     = [\n");
            size_t sz = values.u.arr.size * sizeof(double);
            outputs[i].lut_len = values.u.arr.size;
            outputs[i].input_nits = (double*) malloc(sz);
            outputs[i].output_nits = (double*) malloc(sz);
            for (int j = 0; j < values.u.arr.size; j++) {
                toml_datum_t pair = values.u.arr.elem[j];
                if (pair.type == TOML_ARRAY && pair.u.arr.size == 2) {
                    double in  = toml_to_double(pair.u.arr.elem[0]);
                    double out = toml_to_double(pair.u.arr.elem[1]);
                    outputs[i].input_nits[j] = in;
                    outputs[i].output_nits[j] = out;
                    printf("    [%g, %g]", in, out);
                    if (j + 1 < values.u.arr.size) printf(",");
                    printf("\n");
                }
            }
            printf("  ]\n");
        }

        printf("\n");
    }

    toml_free(result);

    return monitors.u.arr.size;
}

int config_free(output_config *out) {
    free(out->input_nits);
    free(out->output_nits);
    free(out);
}
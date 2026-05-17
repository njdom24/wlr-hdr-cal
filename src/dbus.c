#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <dbus/dbus.h>
#include "dbus.h"
#include "bluelight.h"
#include "cm.h"

static int temperature = 0;

DBusHandlerResult handle_message(DBusConnection *conn, DBusMessage *msg, void *user_data) {
    if (dbus_message_is_method_call(msg, "org.WlrHdrCal", "GetTemperature")) {
        DBusMessage *reply = dbus_message_new_method_return(msg);

        dbus_message_append_args(reply, DBUS_TYPE_INT32, &temperature, DBUS_TYPE_INVALID);

        dbus_connection_send(conn, reply, NULL);
        dbus_connection_flush(conn);

        dbus_message_unref(reply);

        printf("GetTemperature -> %d\n", temperature);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (dbus_message_is_method_call(msg, "org.WlrHdrCal", "SetTemperature")) {
        DBusError err;
        dbus_error_init(&err);

        int32_t new_temperature = 0;

        if (!dbus_message_get_args(msg, &err, DBUS_TYPE_INT32, &new_temperature, DBUS_TYPE_INVALID)) {
            fprintf(stderr, "Failed to parse args: %s\n", err.message);
            dbus_error_free(&err);

            return DBUS_HANDLER_RESULT_HANDLED;
        }

        temperature = new_temperature;

        DBusMessage *reply = dbus_message_new_method_return(msg);

        dbus_connection_send(conn, reply, NULL);
        dbus_connection_flush(conn);

        dbus_message_unref(reply);

        printf("SetTemperature <- %d\n", temperature);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

DBusConnection *setup_dbus() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);

    if (!conn) {
        fprintf(stderr, "Connection failed: %s\n", err.message);
        return NULL;
    }

    int ret = dbus_bus_request_name(conn, "org.WlrHdrCal", DBUS_NAME_FLAG_REPLACE_EXISTING, &err);

    if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        fprintf(stderr, "Failed to request name\n");
        return NULL;
    }

    DBusObjectPathVTable vtable = {
        .message_function = handle_message,
    };

    if (!dbus_connection_register_object_path(conn, "/org/WlrHdrCal", &vtable, NULL)) {
        fprintf(stderr, "Failed to register object path\n");
        return NULL;
    }

    return conn;
}
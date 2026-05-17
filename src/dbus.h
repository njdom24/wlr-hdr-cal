#ifndef Dbus_h
#define Dbus_h
#include <dbus/dbus.h>

DBusHandlerResult handle_message(DBusConnection *conn, DBusMessage *msg, void *user_data);
DBusConnection *setup_dbus();

#endif

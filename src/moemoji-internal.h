#pragma once

#include <gio/gio.h>

char *make_display_name(const char *dirname);
char *find_kaomoji_dir(void);

GVariant *sni_get_property(GDBusConnection *connection, const gchar *sender,
                           const gchar *object_path,
                           const gchar *interface_name,
                           const gchar *property_name, GError **error,
                           gpointer user_data);

GVariant *dbusmenu_get_property(GDBusConnection *connection,
                                const gchar *sender, const gchar *object_path,
                                const gchar *interface_name,
                                const gchar *property_name, GError **error,
                                gpointer user_data);

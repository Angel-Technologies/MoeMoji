#include <glib/gi18n.h>
#include <adwaita.h>

#include "moemoji-config.h"
#include "moemoji-application.h"

static void register_with_portal (void) {
    GError *error = NULL;
    GDBusConnection *bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
    if (bus == NULL) {
        g_warning ("portal register: can't get session bus: %s", error->message);
        g_error_free (error);
        return;
    }
    GVariantBuilder options;
    g_variant_builder_init (&options, G_VARIANT_TYPE ("a{sv}"));
    GVariant *result = g_dbus_connection_call_sync (
        bus,
        "org.freedesktop.portal.Desktop",
        "/org/freedesktop/portal/desktop",
        "org.freedesktop.host.portal.Registry",
        "Register",
        g_variant_new ("(s@a{sv})",
                        "jp.angeltech.MoeMoji",
                        g_variant_builder_end (&options)),
        NULL,
        G_DBUS_CALL_FLAGS_NONE,
        -1, NULL, &error);
    if (result == NULL) {
        g_info ("portal register: %s", error->message);
        g_error_free (error);
    } else {
        g_variant_unref (result);
    }
    g_object_unref (bus);
}

int main (int argc, char *argv[]) {
    bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);
    register_with_portal ();
    g_autoptr(MoeMojiApplication) app =
        moemoji_application_new("jp.angeltech.MoeMoji", G_APPLICATION_DEFAULT_FLAGS);
    return g_application_run(G_APPLICATION(app), argc, argv);
}

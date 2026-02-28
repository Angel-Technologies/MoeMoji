#pragma once

#include "moemoji-window.h"
#include <adwaita.h>

struct _MoeMojiApplication {
  AdwApplication parent_instance;
  GSettings *settings;
  MoeMojiWindow *window;
  gboolean window_created;
  gboolean shortcuts_bound;
  GDBusConnection *dbus_conn;
  guint sni_registration_id;
  guint menu_registration_id;
  gchar *icon_theme_path;
  const gchar *tray_icon_name;
  GDBusProxy *shortcuts_proxy;
  gchar *shortcuts_session_path;
};

G_BEGIN_DECLS

#define MOEMOJI_TYPE_APPLICATION (moemoji_application_get_type())

G_DECLARE_FINAL_TYPE(MoeMojiApplication, moemoji_application, MOEMOJI,
                     APPLICATION, AdwApplication)

MoeMojiApplication *moemoji_application_new(gchar *application_id,
                                            GApplicationFlags flags);

G_END_DECLS

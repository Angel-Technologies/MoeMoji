#include "moemoji-application.h"
#include "moemoji-config.h"
#include "moemoji-internal.h"
#include "moemoji-window.h"
#include <unistd.h>

G_DEFINE_TYPE(MoeMojiApplication, moemoji_application, ADW_TYPE_APPLICATION)

static const gchar sni_introspection_xml[] =
    "<node>"
    "  <interface name='org.kde.StatusNotifierItem'>"
    "    <method name='Activate'>"
    "      <arg type='i' name='x' direction='in'/>"
    "      <arg type='i' name='y' direction='in'/>"
    "    </method>"
    "    <method name='SecondaryActivate'>"
    "      <arg type='i' name='x' direction='in'/>"
    "      <arg type='i' name='y' direction='in'/>"
    "    </method>"
    "    <method name='ContextMenu'>"
    "      <arg type='i' name='x' direction='in'/>"
    "      <arg type='i' name='y' direction='in'/>"
    "    </method>"
    "    <property name='Category' type='s' access='read'/>"
    "    <property name='Id' type='s' access='read'/>"
    "    <property name='Title' type='s' access='read'/>"
    "    <property name='Status' type='s' access='read'/>"
    "    <property name='IconName' type='s' access='read'/>"
    "    <property name='ItemIsMenu' type='b' access='read'/>"
    "    <property name='Menu' type='o' access='read'/>"
    "    <property name='IconThemePath' type='s' access='read'/>"
    "    <signal name='NewIcon'/>"
    "  </interface>"
    "</node>";

static const gchar dbusmenu_introspection_xml[] =
    "<node>"
    "  <interface name='com.canonical.dbusmenu'>"
    "    <method name='GetLayout'>"
    "      <arg type='i' name='parentId' direction='in'/>"
    "      <arg type='i' name='recursionDepth' direction='in'/>"
    "      <arg type='as' name='propertyNames' direction='in'/>"
    "      <arg type='u' name='revision' direction='out'/>"
    "      <arg type='(ia{sv}av)' name='layout' direction='out'/>"
    "    </method>"
    "    <method name='GetGroupProperties'>"
    "      <arg type='ai' name='ids' direction='in'/>"
    "      <arg type='as' name='propertyNames' direction='in'/>"
    "      <arg type='a(ia{sv})' name='properties' direction='out'/>"
    "    </method>"
    "    <method name='GetProperty'>"
    "      <arg type='i' name='id' direction='in'/>"
    "      <arg type='s' name='name' direction='in'/>"
    "      <arg type='v' name='value' direction='out'/>"
    "    </method>"
    "    <method name='Event'>"
    "      <arg type='i' name='id' direction='in'/>"
    "      <arg type='s' name='eventId' direction='in'/>"
    "      <arg type='v' name='data' direction='in'/>"
    "      <arg type='u' name='timestamp' direction='in'/>"
    "    </method>"
    "    <method name='EventGroup'>"
    "      <arg type='a(isvu)' name='events' direction='in'/>"
    "      <arg type='ai' name='idErrors' direction='out'/>"
    "    </method>"
    "    <method name='AboutToShow'>"
    "      <arg type='i' name='id' direction='in'/>"
    "      <arg type='b' name='needUpdate' direction='out'/>"
    "    </method>"
    "    <signal name='ItemsPropertiesUpdated'>"
    "      <arg type='a(ia{sv})' name='updatedProps'/>"
    "      <arg type='a(ias)' name='removedProps'/>"
    "    </signal>"
    "    <signal name='LayoutUpdated'>"
    "      <arg type='u' name='revision'/>"
    "      <arg type='i' name='parent'/>"
    "    </signal>"
    "    <property name='Version' type='u' access='read'/>"
    "    <property name='TextDirection' type='s' access='read'/>"
    "    <property name='Status' type='s' access='read'/>"
    "    <property name='IconThemePath' type='as' access='read'/>"
    "  </interface>"
    "</node>";

static void toggle_window(MoeMojiApplication *self) {
  if (self->window == NULL)
    return;
  GtkWidget *win = GTK_WIDGET(self->window);
  if (gtk_widget_get_visible(win)) {
    gtk_widget_set_visible(win, FALSE);
  } else {
    gtk_window_present(GTK_WINDOW(win));
  }
}

static gboolean on_close_request(GtkWindow *window, gpointer user_data) {
  MoeMojiApplication *self = MOEMOJI_APPLICATION(user_data);
  if (self->has_tray) {
    gtk_widget_set_visible(GTK_WIDGET(window), FALSE);
    return TRUE;
  }
  return FALSE;
}

static void dbusmenu_method_call(G_GNUC_UNUSED GDBusConnection *connection,
                                 G_GNUC_UNUSED const gchar *sender,
                                 G_GNUC_UNUSED const gchar *object_path,
                                 G_GNUC_UNUSED const gchar *interface_name,
                                 const gchar *method_name, GVariant *parameters,
                                 GDBusMethodInvocation *invocation,
                                 gpointer user_data) {
  MoeMojiApplication *self = MOEMOJI_APPLICATION(user_data);

  if (g_strcmp0(method_name, "GetLayout") == 0) {
    GVariantBuilder root_props;
    g_variant_builder_init(&root_props, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&root_props, "{sv}", "children-display",
                          g_variant_new_string("submenu"));

    GVariantBuilder quit_props;
    g_variant_builder_init(&quit_props, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&quit_props, "{sv}", "label",
                          g_variant_new_string("Quit"));
    g_variant_builder_add(&quit_props, "{sv}", "icon-name",
                          g_variant_new_string("application-exit"));

    GVariantBuilder quit_children;
    g_variant_builder_init(&quit_children, G_VARIANT_TYPE("av"));

    GVariant *quit_item =
        g_variant_new("(ia{sv}av)", 1, &quit_props, &quit_children);

    GVariantBuilder root_children;
    g_variant_builder_init(&root_children, G_VARIANT_TYPE("av"));
    g_variant_builder_add(&root_children, "v", quit_item);

    GVariant *layout =
        g_variant_new("(ia{sv}av)", 0, &root_props, &root_children);

    g_dbus_method_invocation_return_value(
        invocation, g_variant_new("(u@(ia{sv}av))", 1, layout));

  } else if (g_strcmp0(method_name, "GetGroupProperties") == 0) {
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a(ia{sv})"));
    g_dbus_method_invocation_return_value(
        invocation,
        g_variant_new("(@a(ia{sv}))", g_variant_builder_end(&builder)));

  } else if (g_strcmp0(method_name, "GetProperty") == 0) {
    g_dbus_method_invocation_return_value(
        invocation, g_variant_new("(v)", g_variant_new_string("")));

  } else if (g_strcmp0(method_name, "Event") == 0) {
    gint32 id;
    const gchar *event_id;
    g_variant_get(parameters, "(i&sv@u)", &id, &event_id, NULL, NULL);

    if (id == 1 && g_strcmp0(event_id, "clicked") == 0) {
      g_application_quit(G_APPLICATION(self));
    }
    g_dbus_method_invocation_return_value(invocation, NULL);

  } else if (g_strcmp0(method_name, "EventGroup") == 0) {
    GVariant *events = g_variant_get_child_value(parameters, 0);
    GVariantIter iter;
    g_variant_iter_init(&iter, events);
    gint32 id;
    const gchar *event_id;
    while (g_variant_iter_next(&iter, "(i&svu)", &id, &event_id, NULL, NULL)) {
      if (id == 1 && g_strcmp0(event_id, "clicked") == 0) {
        g_application_quit(G_APPLICATION(self));
      }
    }
    g_variant_unref(events);

    GVariantBuilder errors;
    g_variant_builder_init(&errors, G_VARIANT_TYPE("ai"));
    g_dbus_method_invocation_return_value(
        invocation, g_variant_new("(@ai)", g_variant_builder_end(&errors)));

  } else if (g_strcmp0(method_name, "AboutToShow") == 0) {
    g_dbus_method_invocation_return_value(invocation,
                                          g_variant_new("(b)", FALSE));

  } else {
    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                          G_DBUS_ERROR_UNKNOWN_METHOD,
                                          "Unknown method: %s", method_name);
  }
}

GVariant *dbusmenu_get_property(G_GNUC_UNUSED GDBusConnection *connection,
                                G_GNUC_UNUSED const gchar *sender,
                                G_GNUC_UNUSED const gchar *object_path,
                                G_GNUC_UNUSED const gchar *interface_name,
                                const gchar *property_name,
                                G_GNUC_UNUSED GError **error,
                                G_GNUC_UNUSED gpointer user_data) {
  if (g_strcmp0(property_name, "Version") == 0)
    return g_variant_new_uint32(3);
  if (g_strcmp0(property_name, "TextDirection") == 0)
    return g_variant_new_string("ltr");
  if (g_strcmp0(property_name, "Status") == 0)
    return g_variant_new_string("normal");
  if (g_strcmp0(property_name, "IconThemePath") == 0)
    return g_variant_new_strv(NULL, 0);
  return NULL;
}

static const GDBusInterfaceVTable dbusmenu_vtable = {
    .method_call = dbusmenu_method_call,
    .get_property = dbusmenu_get_property,
    .set_property = NULL,
};

static void sni_method_call(G_GNUC_UNUSED GDBusConnection *connection,
                            G_GNUC_UNUSED const gchar *sender,
                            G_GNUC_UNUSED const gchar *object_path,
                            G_GNUC_UNUSED const gchar *interface_name,
                            const gchar *method_name,
                            G_GNUC_UNUSED GVariant *parameters,
                            GDBusMethodInvocation *invocation,
                            gpointer user_data) {
  MoeMojiApplication *self = MOEMOJI_APPLICATION(user_data);

  if (g_strcmp0(method_name, "Activate") == 0) {
    toggle_window(self);
  }

  g_dbus_method_invocation_return_value(invocation, NULL);
}

GVariant *sni_get_property(G_GNUC_UNUSED GDBusConnection *connection,
                           G_GNUC_UNUSED const gchar *sender,
                           G_GNUC_UNUSED const gchar *object_path,
                           G_GNUC_UNUSED const gchar *interface_name,
                           const gchar *property_name,
                           G_GNUC_UNUSED GError **error, gpointer user_data) {
  if (g_strcmp0(property_name, "Category") == 0)
    return g_variant_new_string("ApplicationStatus");
  if (g_strcmp0(property_name, "Id") == 0)
    return g_variant_new_string("moemoji");
  if (g_strcmp0(property_name, "Title") == 0)
    return g_variant_new_string("MoeMoji");
  if (g_strcmp0(property_name, "Status") == 0)
    return g_variant_new_string("Active");
  if (g_strcmp0(property_name, "IconName") == 0) {
    MoeMojiApplication *self = MOEMOJI_APPLICATION(user_data);
    return g_variant_new_string(self->tray_icon_name
                                    ? self->tray_icon_name
                                    : "jp.angeltech.MoeMoji-tray-dark");
  }
  if (g_strcmp0(property_name, "ItemIsMenu") == 0)
    return g_variant_new_boolean(FALSE);
  if (g_strcmp0(property_name, "Menu") == 0)
    return g_variant_new_object_path("/MenuBar");
  if (g_strcmp0(property_name, "IconThemePath") == 0) {
    MoeMojiApplication *self = MOEMOJI_APPLICATION(user_data);
    return g_variant_new_string(self->icon_theme_path ? self->icon_theme_path
                                                      : "");
  }
  return NULL;
}

static const GDBusInterfaceVTable sni_vtable = {
    .method_call = sni_method_call,
    .get_property = sni_get_property,
    .set_property = NULL,
};

static void update_tray_icon(MoeMojiApplication *self) {
  AdwStyleManager *sm = adw_style_manager_get_default();
  gboolean dark = adw_style_manager_get_dark(sm);
  self->tray_icon_name = dark ? "jp.angeltech.MoeMoji-tray-dark"
                              : "jp.angeltech.MoeMoji-tray-light";

  if (self->dbus_conn && self->sni_registration_id > 0) {
    g_dbus_connection_emit_signal(self->dbus_conn, NULL, "/StatusNotifierItem",
                                  "org.kde.StatusNotifierItem", "NewIcon", NULL,
                                  NULL);
  }
}

static void on_dark_changed(G_GNUC_UNUSED GObject *obj,
                            G_GNUC_UNUSED GParamSpec *pspec,
                            gpointer user_data) {
  update_tray_icon(MOEMOJI_APPLICATION(user_data));
}

static void setup_sni(MoeMojiApplication *self) {
  GError *error = NULL;
  self->dbus_conn = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
  if (self->dbus_conn == NULL) {
    g_warning("session bus: %s", error->message);
    g_clear_error(&error);
    self->has_tray = FALSE;
    return;
  }

  GVariant *result = g_dbus_connection_call_sync(
      self->dbus_conn, "org.freedesktop.DBus", "/org/freedesktop/DBus",
      "org.freedesktop.DBus", "NameHasOwner",
      g_variant_new("(s)", "org.kde.StatusNotifierWatcher"),
      G_VARIANT_TYPE("(b)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
  if (result == NULL) {
    g_warning("NameHasOwner check failed: %s", error->message);
    g_clear_error(&error);
    self->has_tray = FALSE;
    return;
  }

  gboolean watcher_exists = FALSE;
  g_variant_get(result, "(b)", &watcher_exists);
  g_variant_unref(result);

  if (!watcher_exists) {
    g_info("No StatusNotifierWatcher found, skipping tray setup");
    self->has_tray = FALSE;
    return;
  }

  self->has_tray = TRUE;
  GDBusNodeInfo *sni_node =
      g_dbus_node_info_new_for_xml(sni_introspection_xml, &error);
  if (sni_node == NULL) {
    g_warning("Failed to parse SNI introspection XML: %s", error->message);
    g_clear_error(&error);
    return;
  }
  self->sni_registration_id = g_dbus_connection_register_object(
      self->dbus_conn, "/StatusNotifierItem", sni_node->interfaces[0],
      &sni_vtable, self, NULL, &error);
  g_dbus_node_info_unref(sni_node);
  if (self->sni_registration_id == 0) {
    g_warning("Failed to register SNI object: %s", error->message);
    g_clear_error(&error);
    return;
  }
  GDBusNodeInfo *menu_node =
      g_dbus_node_info_new_for_xml(dbusmenu_introspection_xml, &error);
  if (menu_node == NULL) {
    g_warning("Failed to parse dbusmenu introspection XML: %s", error->message);
    g_clear_error(&error);
    return;
  }
  self->menu_registration_id = g_dbus_connection_register_object(
      self->dbus_conn, "/MenuBar", menu_node->interfaces[0], &dbusmenu_vtable,
      self, NULL, &error);
  g_dbus_node_info_unref(menu_node);
  if (self->menu_registration_id == 0) {
    g_warning("dbusmenu register: %s", error->message);
    g_clear_error(&error);
  }
  const gchar *unique_name = g_dbus_connection_get_unique_name(self->dbus_conn);
  g_dbus_connection_call(
      self->dbus_conn, "org.kde.StatusNotifierWatcher",
      "/StatusNotifierWatcher", "org.kde.StatusNotifierWatcher",
      "RegisterStatusNotifierItem", g_variant_new("(s)", unique_name), NULL,
      G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
}

static void on_shortcuts_activated(G_GNUC_UNUSED GDBusProxy *proxy,
                                   G_GNUC_UNUSED const gchar *sender_name,
                                   const gchar *signal_name,
                                   GVariant *parameters, gpointer user_data) {
  if (g_strcmp0(signal_name, "Activated") != 0)
    return;
  MoeMojiApplication *self = MOEMOJI_APPLICATION(user_data);
  const gchar *shortcut_id = NULL;
  g_variant_get_child(parameters, 1, "&s", &shortcut_id);
  if (g_strcmp0(shortcut_id, "toggle-window") == 0)
    toggle_window(self);
}

static void
on_bind_shortcuts_response(G_GNUC_UNUSED GDBusConnection *connection,
                           G_GNUC_UNUSED const gchar *sender_name,
                           G_GNUC_UNUSED const gchar *object_path,
                           G_GNUC_UNUSED const gchar *interface_name,
                           G_GNUC_UNUSED const gchar *signal_name,
                           GVariant *parameters,
                           G_GNUC_UNUSED gpointer user_data) {
  guint32 response;
  g_autoptr(GVariant) results = NULL;
  g_variant_get(parameters, "(u@a{sv})", &response, &results);
  if (response != 0)
    g_warning("BindShortcuts portal response: %u", response);
}

static void bind_shortcuts(MoeMojiApplication *self) {
  GVariantBuilder shortcut_builder;
  g_variant_builder_init(&shortcut_builder, G_VARIANT_TYPE("a(sa{sv})"));
  g_variant_builder_open(&shortcut_builder, G_VARIANT_TYPE("(sa{sv})"));
  g_variant_builder_add(&shortcut_builder, "s", "toggle-window");
  GVariantBuilder props_builder;
  g_variant_builder_init(&props_builder, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&props_builder, "{sv}", "description",
                        g_variant_new_string("Toggle MoeMoji"));
  g_variant_builder_add(&props_builder, "{sv}", "preferred_trigger",
                        g_variant_new_string("SHIFT+LOGO+E"));
  g_variant_builder_add_value(&shortcut_builder,
                              g_variant_builder_end(&props_builder));
  g_variant_builder_close(&shortcut_builder);
  GVariantBuilder options_builder;
  g_variant_builder_init(&options_builder, G_VARIANT_TYPE("a{sv}"));

  g_autofree gchar *token = g_strdup_printf("moemoji_bind_%d", getpid());
  const gchar *unique = g_dbus_connection_get_unique_name(self->dbus_conn);
  g_autofree gchar *unique_escaped = g_strdup(unique + 1);
  for (gchar *p = unique_escaped; *p; p++) {
    if (*p == '.')
      *p = '_';
  }
  g_autofree gchar *request_path = g_strdup_printf(
      "/org/freedesktop/portal/desktop/request/%s/%s", unique_escaped, token);

  g_variant_builder_add(&options_builder, "{sv}", "handle_token",
                        g_variant_new_string(token));

  g_dbus_connection_signal_subscribe(
      self->dbus_conn, "org.freedesktop.portal.Desktop",
      "org.freedesktop.portal.Request", "Response", request_path, NULL,
      G_DBUS_SIGNAL_FLAGS_NO_MATCH_RULE, on_bind_shortcuts_response, self,
      NULL);

  const gchar *parent = "";
  g_dbus_proxy_call(
      self->shortcuts_proxy, "BindShortcuts",
      g_variant_new("(o@a(sa{sv})s@a{sv})", self->shortcuts_session_path,
                    g_variant_builder_end(&shortcut_builder), parent,
                    g_variant_builder_end(&options_builder)),
      G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
}

static void
on_create_session_response(G_GNUC_UNUSED GDBusConnection *connection,
                           G_GNUC_UNUSED const gchar *sender_name,
                           G_GNUC_UNUSED const gchar *object_path,
                           G_GNUC_UNUSED const gchar *interface_name,
                           G_GNUC_UNUSED const gchar *signal_name,
                           GVariant *parameters, gpointer user_data) {
  MoeMojiApplication *self = MOEMOJI_APPLICATION(user_data);
  guint32 response;
  g_autoptr(GVariant) results = NULL;
  g_variant_get(parameters, "(u@a{sv})", &response, &results);
  if (response != 0) {
    g_warning("GlobalShortcuts CreateSession failed with response %u",
              response);
    return;
  }
  const gchar *session_handle = NULL;
  g_variant_lookup(results, "session_handle", "&s", &session_handle);
  if (session_handle == NULL) {
    g_warning("GlobalShortcuts: No session_handle in CreateSession response");
    return;
  }
  self->shortcuts_session_path = g_strdup(session_handle);
  g_signal_connect(self->shortcuts_proxy, "g-signal",
                   G_CALLBACK(on_shortcuts_activated), self);
  bind_shortcuts(self);
}

static gboolean begin_create_session(MoeMojiApplication *self) {
  const gchar *session_token = "moemoji_session";
  g_autofree gchar *handle_token =
      g_strdup_printf("moemoji_handle_%d", getpid());
  const gchar *unique = g_dbus_connection_get_unique_name(self->dbus_conn);
  g_autofree gchar *unique_escaped = g_strdup(unique + 1);
  for (gchar *p = unique_escaped; *p; p++) {
    if (*p == '.')
      *p = '_';
  }
  g_autofree gchar *request_path =
      g_strdup_printf("/org/freedesktop/portal/desktop/request/%s/%s",
                      unique_escaped, handle_token);

  g_dbus_connection_signal_subscribe(
      self->dbus_conn, "org.freedesktop.portal.Desktop",
      "org.freedesktop.portal.Request", "Response", request_path, NULL,
      G_DBUS_SIGNAL_FLAGS_NO_MATCH_RULE, on_create_session_response, self,
      NULL);
  GVariantBuilder options;
  g_variant_builder_init(&options, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&options, "{sv}", "session_handle_token",
                        g_variant_new_string(session_token));
  g_variant_builder_add(&options, "{sv}", "handle_token",
                        g_variant_new_string(handle_token));

  g_dbus_proxy_call(self->shortcuts_proxy, "CreateSession",
                    g_variant_new("(@a{sv})", g_variant_builder_end(&options)),
                    G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
  return G_SOURCE_REMOVE;
}

static void setup_global_shortcuts(MoeMojiApplication *self) {
  GError *error = NULL;
  if (self->dbus_conn == NULL)
    return;
  if (self->shortcuts_bound)
    return;
  self->shortcuts_bound = TRUE;
  self->shortcuts_proxy = g_dbus_proxy_new_sync(
      self->dbus_conn,
      G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
          G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
      NULL, "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
      "org.freedesktop.portal.GlobalShortcuts", NULL, &error);
  if (self->shortcuts_proxy == NULL) {
    g_warning("GlobalShortcuts: Failed to create proxy: %s", error->message);
    g_clear_error(&error);
    return;
  }
  g_timeout_add(500, (GSourceFunc)begin_create_session, self);
}

MoeMojiApplication *moemoji_application_new(gchar *application_id,
                                            GApplicationFlags flags) {
  return g_object_new(MOEMOJI_TYPE_APPLICATION, "application-id",
                      application_id, "flags", flags, NULL);
}

static void moemoji_application_startup(GApplication *app) {
  G_APPLICATION_CLASS(moemoji_application_parent_class)->startup(app);
  MoeMojiApplication *self = MOEMOJI_APPLICATION(app);
  gtk_icon_theme_add_resource_path(
      gtk_icon_theme_get_for_display(gdk_display_get_default()),
      "/jp/angeltech/MoeMoji/icons");
  gtk_window_set_default_icon_name("jp.angeltech.MoeMoji");
  gboolean in_flatpak = g_file_test("/.flatpak-info", G_FILE_TEST_EXISTS);
  if (in_flatpak) {
    self->icon_theme_path = NULL;
  } else {
    const char *src_dir = g_getenv("MESON_SOURCE_ROOT");
    if (src_dir) {
      self->icon_theme_path = g_build_filename(src_dir, "data", "icons", NULL);
    } else {
      g_autofree char *cwd_icons =
          g_build_filename("data", "icons", "hicolor", NULL);
      if (g_file_test(cwd_icons, G_FILE_TEST_IS_DIR)) {
        g_autofree char *cwd = g_get_current_dir();
        self->icon_theme_path = g_build_filename(cwd, "data", "icons", NULL);
      } else {
        self->icon_theme_path =
            g_build_filename(MOEMOJI_DATADIR, "..", "icons", NULL);
      }
    }
  }
  setup_sni(self);
  update_tray_icon(self);
  g_signal_connect(adw_style_manager_get_default(), "notify::dark",
                   G_CALLBACK(on_dark_changed), self);
  setup_global_shortcuts(self);
}

static void moemoji_application_activate(GApplication *app) {
  g_assert(MOEMOJI_IS_APPLICATION(app));
  MoeMojiApplication *self = MOEMOJI_APPLICATION(app);
  if (!self->window_created) {
    self->window = g_object_new(MOEMOJI_TYPE_WINDOW, "application",
                                GTK_APPLICATION(self), NULL);
    g_signal_connect(self->window, "close-request",
                     G_CALLBACK(on_close_request), self);
    self->window_created = TRUE;
  } else {
    toggle_window(self);
  }
}

static void moemoji_application_dispose(GObject *object) {
  MoeMojiApplication *self = MOEMOJI_APPLICATION(object);
  if (self->sni_registration_id > 0 && self->dbus_conn != NULL) {
    g_dbus_connection_unregister_object(self->dbus_conn,
                                        self->sni_registration_id);
    self->sni_registration_id = 0;
  }
  if (self->menu_registration_id > 0 && self->dbus_conn != NULL) {
    g_dbus_connection_unregister_object(self->dbus_conn,
                                        self->menu_registration_id);
    self->menu_registration_id = 0;
  }
  g_clear_object(&self->shortcuts_proxy);
  g_clear_pointer(&self->shortcuts_session_path, g_free);
  g_clear_pointer(&self->icon_theme_path, g_free);
  g_clear_object(&self->dbus_conn);
  G_OBJECT_CLASS(moemoji_application_parent_class)->dispose(object);
}

static void moemoji_application_show_about(G_GNUC_UNUSED GSimpleAction *action,
                                           G_GNUC_UNUSED GVariant *parameter,
                                           gpointer user_data) {
  MoeMojiApplication *self = MOEMOJI_APPLICATION(user_data);
  GtkWindow *window = NULL;
  g_return_if_fail(MOEMOJI_IS_APPLICATION(self));
  window = gtk_application_get_active_window(GTK_APPLICATION(self));
  gtk_show_about_dialog(window, "program-name", "MoeMoji", "version",
                        PACKAGE_VERSION, NULL);
}

static void moemoji_application_class_init(MoeMojiApplicationClass *klass) {
  GApplicationClass *app_class = G_APPLICATION_CLASS(klass);
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  app_class->startup = moemoji_application_startup;
  app_class->activate = moemoji_application_activate;
  object_class->dispose = moemoji_application_dispose;
}

static void moemoji_application_init(MoeMojiApplication *self) {
  self->settings = g_settings_new("jp.angeltech.MoeMoji");
  self->window_created = FALSE;
  self->shortcuts_bound = FALSE;
  g_autoptr(GSimpleAction) quit_action = g_simple_action_new("quit", NULL);
  g_signal_connect_swapped(quit_action, "activate",
                           G_CALLBACK(g_application_quit), self);
  g_action_map_add_action(G_ACTION_MAP(self), G_ACTION(quit_action));
  g_autoptr(GSimpleAction) about_action = g_simple_action_new("about", NULL);
  g_signal_connect(about_action, "activate",
                   G_CALLBACK(moemoji_application_show_about), self);
  g_action_map_add_action(G_ACTION_MAP(self), G_ACTION(about_action));
  gtk_application_set_accels_for_action(GTK_APPLICATION(self), "app.quit",
                                        (const char *[]){
                                            "<primary>q",
                                            NULL,
                                        });
}

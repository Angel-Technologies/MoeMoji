#include "moemoji-window.h"
#include "moemoji-config.h"
#include "moemoji-internal.h"

#include <adwaita.h>
#include <glib/gstdio.h>
#include <string.h>

G_DEFINE_TYPE(MoeMojiWindow, moemoji_window, GTK_TYPE_APPLICATION_WINDOW)

static void reload_categories(MoeMojiWindow *self);
static void copy_dir_recursive(const char *src, const char *dst);
static void remove_dir_recursive(const char *path);

static void category_widgets_free(gpointer data) {
  CategoryWidgets *cw = data;
  g_free(cw->name);
  g_free(cw->path);
  g_free(cw);
}

char *make_display_name(const char *dirname) {
  char *name = g_strdup(dirname);
  for (char *p = name; *p; p++) {
    if (*p == '_')
      *p = ' ';
  }
  return name;
}

static char *get_category_display_name(const char *cat_path) {
  g_autofree char *name_file = g_build_filename(cat_path, ".name", NULL);
  char *contents = NULL;
  if (g_file_get_contents(name_file, &contents, NULL, NULL)) {
    g_strchomp(contents);
    if (contents[0] != '\0')
      return contents;
    g_free(contents);
  }
  g_autofree char *basename = g_path_get_basename(cat_path);
  return make_display_name(basename);
}

char *find_kaomoji_dir(void) {
  const char *src_dir = g_getenv("MESON_SOURCE_ROOT");
  if (src_dir) {
    char *dev_path = g_build_filename(src_dir, "data", "kaomoji", NULL);
    if (g_file_test(dev_path, G_FILE_TEST_IS_DIR))
      return dev_path;
    g_free(dev_path);
  }
  char *cwd_path = g_build_filename("data", "kaomoji", NULL);
  if (g_file_test(cwd_path, G_FILE_TEST_IS_DIR))
    return cwd_path;
  g_free(cwd_path);
  char *installed = g_build_filename(MOEMOJI_DATADIR, "kaomoji", NULL);
  if (g_file_test(installed, G_FILE_TEST_IS_DIR))
    return installed;
  g_free(installed);

  return NULL;
}

static char *user_kaomoji_dir(void) {
  return g_build_filename(g_get_user_data_dir(), "moemoji", "kaomoji", NULL);
}

static void copy_builtins_to_user_dir(void) {
  g_autofree char *dst = user_kaomoji_dir();
  g_mkdir_with_parents(dst, 0755);
  char *src = find_kaomoji_dir();
  if (!src)
    return;
  GDir *dir = g_dir_open(src, 0, NULL);
  if (dir) {
    const char *name;
    while ((name = g_dir_read_name(dir)) != NULL) {
      g_autofree char *s = g_build_filename(src, name, NULL);
      g_autofree char *d = g_build_filename(dst, name, NULL);
      if (g_file_test(s, G_FILE_TEST_IS_DIR) &&
          !g_file_test(d, G_FILE_TEST_EXISTS))
        copy_dir_recursive(s, d);
    }
    g_dir_close(dir);
  }
  g_free(src);
}

static void first_launch_setup(void) {
  g_autofree char *dir = user_kaomoji_dir();
  if (g_file_test(dir, G_FILE_TEST_IS_DIR)) {
    GDir *d = g_dir_open(dir, 0, NULL);
    if (d) {
      gboolean has_entries = g_dir_read_name(d) != NULL;
      g_dir_close(d);
      if (has_entries)
        return;
    }
  }
  copy_builtins_to_user_dir();
}

static void on_kaomoji_clicked(GtkButton *button, gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  const char *full = g_object_get_data(G_OBJECT(button), "full-text");
  const char *text = full ? full : gtk_button_get_label(button);
  GdkClipboard *clipboard = gtk_widget_get_clipboard(GTK_WIDGET(button));
  gdk_clipboard_set_text(clipboard, text);
  AdwToast *toast = adw_toast_new("Copied!");
  adw_toast_set_timeout(toast, 1);
  adw_toast_overlay_add_toast(self->toast_overlay, toast);
}

static void on_popover_enter(G_GNUC_UNUSED GtkEventControllerMotion *ctrl,
                             G_GNUC_UNUSED double x, G_GNUC_UNUSED double y,
                             gpointer user_data) {
  gtk_popover_popup(GTK_POPOVER(user_data));
}

static void on_popover_leave(G_GNUC_UNUSED GtkEventControllerMotion *ctrl,
                             gpointer user_data) {
  gtk_popover_popdown(GTK_POPOVER(user_data));
}

static void on_button_destroy(GtkWidget *button,
                              G_GNUC_UNUSED gpointer user_data) {
  GtkWidget *popover = g_object_get_data(G_OBJECT(button), "popover");
  if (popover)
    gtk_widget_unparent(popover);
}

static void on_context_popover_closed(GtkPopover *popover,
                                      G_GNUC_UNUSED gpointer user_data) {
  GtkWidget *parent = gtk_widget_get_parent(GTK_WIDGET(popover));
  if (parent) {
    gtk_widget_remove_css_class(parent, "context-active");
    g_object_set_data(G_OBJECT(parent), "ctx-popover", NULL);
    gtk_widget_unparent(GTK_WIDGET(popover));
  }
}

static void on_ctx_parent_destroy(GtkWidget *parent,
                                  G_GNUC_UNUSED gpointer user_data) {
  GtkWidget *popover = g_object_get_data(G_OBJECT(parent), "ctx-popover");
  if (popover)
    gtk_widget_unparent(popover);
}

static gboolean enable_popover_buttons_idle(gpointer data) {
  GtkWidget *box = GTK_WIDGET(data);
  for (GtkWidget *child = gtk_widget_get_first_child(box); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    gtk_widget_set_can_target(child, TRUE);
  }
  return G_SOURCE_REMOVE;
}

static GtkWidget *ctx_add_button(GtkBox *box, const char *label,
                                 GCallback callback, gpointer user_data) {
  GtkWidget *btn = gtk_button_new_with_label(label);
  gtk_widget_add_css_class(btn, "flat");
  gtk_widget_set_can_target(btn, FALSE);
  gtk_widget_set_focusable(btn, FALSE);
  g_signal_connect(btn, "clicked", callback, user_data);
  gtk_box_append(box, btn);
  return btn;
}

static void show_context_popover(GtkWidget *parent, GtkWidget *box, double x,
                                 double y) {
  gtk_widget_add_css_class(parent, "context-active");

  GtkWidget *old = g_object_get_data(G_OBJECT(parent), "ctx-popover");
  if (old)
    gtk_widget_unparent(old);

  GtkWidget *popover = gtk_popover_new();
  gtk_widget_set_parent(popover, parent);
  gtk_widget_add_css_class(popover, "context-menu-popover");
  gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
  GdkRectangle rect = {(int)x, (int)y, 1, 1};
  gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
  gtk_popover_set_child(GTK_POPOVER(popover), box);
  g_object_set_data(G_OBJECT(parent), "ctx-popover", popover);
  g_signal_connect(popover, "closed", G_CALLBACK(on_context_popover_closed),
                   NULL);
  g_signal_connect(parent, "destroy", G_CALLBACK(on_ctx_parent_destroy), NULL);
  gtk_popover_popup(GTK_POPOVER(popover));
  g_idle_add(enable_popover_buttons_idle, box);
}

static void rebuild_pinned_box(MoeMojiWindow *self);

/* Build a flat chip button with an ellipsizing left-aligned label. */
static GtkWidget *make_chip_button(const char *label_text) {
  GtkWidget *btn = gtk_button_new_with_label(label_text);
  gtk_widget_add_css_class(btn, "category-chip");
  gtk_widget_add_css_class(btn, "flat");
  gtk_button_set_has_frame(GTK_BUTTON(btn), FALSE);
  GtkWidget *lbl = gtk_button_get_child(GTK_BUTTON(btn));
  gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
  gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_END);
  return btn;
}

static gboolean is_kaomoji_pinned(MoeMojiWindow *self, const char *text) {
  g_auto(GStrv) pinned = g_settings_get_strv(self->settings, "pinned-kaomojis");
  return g_strv_contains((const char *const *)pinned, text);
}

static void on_ctx_pin_emote(G_GNUC_UNUSED GSimpleAction *action,
                             G_GNUC_UNUSED GVariant *parameter,
                             gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  const char *text = self->ctx_emote_text;
  if (!text || is_kaomoji_pinned(self, text))
    return;
  g_auto(GStrv) pinned = g_settings_get_strv(self->settings, "pinned-kaomojis");
  guint len = g_strv_length(pinned);
  char **new_pinned = g_new(char *, len + 2);
  for (guint i = 0; i < len; i++)
    new_pinned[i] = pinned[i];
  new_pinned[len] = g_strdup(text);
  new_pinned[len + 1] = NULL;
  g_settings_set_strv(self->settings, "pinned-kaomojis",
                      (const char *const *)new_pinned);
  g_free(new_pinned[len]);
  g_free(new_pinned);
  rebuild_pinned_box(self);
}

static void on_ctx_unpin_emote(G_GNUC_UNUSED GSimpleAction *action,
                               G_GNUC_UNUSED GVariant *parameter,
                               gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  const char *text = self->ctx_emote_text;
  if (!text)
    return;
  g_auto(GStrv) pinned = g_settings_get_strv(self->settings, "pinned-kaomojis");
  GPtrArray *new_arr = g_ptr_array_new();
  for (guint i = 0; pinned[i]; i++) {
    if (g_strcmp0(pinned[i], text) != 0)
      g_ptr_array_add(new_arr, pinned[i]);
  }
  g_ptr_array_add(new_arr, NULL);
  g_settings_set_strv(self->settings, "pinned-kaomojis",
                      (const char *const *)new_arr->pdata);
  g_ptr_array_free(new_arr, TRUE);
  rebuild_pinned_box(self);
}

static void on_ctx_action_clicked(GtkButton *button, gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  const char *action = g_object_get_data(G_OBJECT(button), "action-name");
  if (!action)
    return;
  GtkWidget *popover =
      gtk_widget_get_ancestor(GTK_WIDGET(button), GTK_TYPE_POPOVER);
  g_action_group_activate_action(G_ACTION_GROUP(self), action, NULL);
  if (popover)
    gtk_popover_popdown(GTK_POPOVER(popover));
}

static void on_pinned_right_click(GtkGestureClick *gesture,
                                  G_GNUC_UNUSED int n_press, double x, double y,
                                  gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  GtkWidget *button =
      gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
  const char *full_text = g_object_get_data(G_OBJECT(button), "full-text");
  const char *text =
      full_text ? full_text : gtk_button_get_label(GTK_BUTTON(button));

  g_free(self->ctx_emote_text);
  self->ctx_emote_text = g_strdup(text);

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  GtkWidget *unpin_btn = ctx_add_button(
      GTK_BOX(box), "Unpin", G_CALLBACK(on_ctx_action_clicked), self);
  g_object_set_data(G_OBJECT(unpin_btn), "action-name", "ctx-unpin-emote");
  show_context_popover(button, box, x, y);
}

static void on_kaomoji_right_click(GtkGestureClick *gesture,
                                   G_GNUC_UNUSED int n_press, double x,
                                   double y, gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  GtkWidget *button =
      gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
  const char *filepath = g_object_get_data(G_OBJECT(button), "emote-path");
  if (!filepath)
    return;
  const char *full_text = g_object_get_data(G_OBJECT(button), "full-text");
  const char *emote_text =
      full_text ? full_text : gtk_button_get_label(GTK_BUTTON(button));

  g_free(self->ctx_emote_path);
  self->ctx_emote_path = g_strdup(filepath);
  g_free(self->ctx_emote_text);
  self->ctx_emote_text = g_strdup(emote_text);

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  GtkWidget *pin_btn;
  if (is_kaomoji_pinned(self, emote_text)) {
    pin_btn = ctx_add_button(GTK_BOX(box), "Unpin",
                             G_CALLBACK(on_ctx_action_clicked), self);
    g_object_set_data(G_OBJECT(pin_btn), "action-name", "ctx-unpin-emote");
  } else {
    pin_btn = ctx_add_button(GTK_BOX(box), "Pin",
                             G_CALLBACK(on_ctx_action_clicked), self);
    g_object_set_data(G_OBJECT(pin_btn), "action-name", "ctx-pin-emote");
  }
  GtkWidget *del_btn = ctx_add_button(GTK_BOX(box), "Delete",
                                      G_CALLBACK(on_ctx_action_clicked), self);
  g_object_set_data(G_OBJECT(del_btn), "action-name", "ctx-delete-emote");
  show_context_popover(button, box, x, y);
}

static void add_kaomoji_button(MoeMojiWindow *self, GtkFlowBox *flow,
                               const char *text, const char *filepath) {
  gboolean multiline = (strchr(text, '\n') != NULL);
  const char *label_text = text;
  g_autofree char *first_line = NULL;
  if (multiline) {
    const char *nl = strchr(text, '\n');
    first_line = g_strndup(text, nl - text);
    label_text = first_line;
  }
  GtkWidget *button = gtk_button_new_with_label(label_text);
  gtk_widget_set_halign(button, GTK_ALIGN_FILL);
  GtkWidget *label = gtk_button_get_child(GTK_BUTTON(button));
  gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
  gtk_label_set_xalign(GTK_LABEL(label), 0.0);
  gtk_widget_add_css_class(button, "kaomoji-button");
  gtk_widget_add_css_class(button, "flat");
  if (multiline || g_utf8_strlen(label_text, -1) > 20)
    gtk_widget_add_css_class(button, "kaomoji-wide");
  if (multiline)
    g_object_set_data_full(G_OBJECT(button), "full-text", g_strdup(text),
                           g_free);

  if (filepath)
    g_object_set_data_full(G_OBJECT(button), "emote-path", g_strdup(filepath),
                           g_free);

  GtkGestureClick *right_click = GTK_GESTURE_CLICK(gtk_gesture_click_new());
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(right_click), 3);
  g_signal_connect(right_click, "pressed", G_CALLBACK(on_kaomoji_right_click),
                   self);
  gtk_widget_add_controller(button, GTK_EVENT_CONTROLLER(right_click));

  g_signal_connect(button, "clicked", G_CALLBACK(on_kaomoji_clicked), self);
  if (multiline) {
    GtkWidget *popover = gtk_popover_new();
    gtk_popover_set_autohide(GTK_POPOVER(popover), FALSE);
    GtkWidget *label = gtk_label_new(text);
    gtk_widget_add_css_class(label, "kaomoji-preview");
    gtk_popover_set_child(GTK_POPOVER(popover), label);
    gtk_widget_set_parent(popover, button);
    g_object_set_data(G_OBJECT(button), "popover", popover);
    g_signal_connect(button, "destroy", G_CALLBACK(on_button_destroy), NULL);

    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "enter", G_CALLBACK(on_popover_enter), popover);
    g_signal_connect(motion, "leave", G_CALLBACK(on_popover_leave), popover);
    gtk_widget_add_controller(button, motion);
  }
  gtk_flow_box_insert(flow, button, -1);
}

static guint64 get_file_mtime(const char *path) {
  GFile *f = g_file_new_for_path(path);
  GFileInfo *info = g_file_query_info(f, G_FILE_ATTRIBUTE_TIME_MODIFIED,
                                      G_FILE_QUERY_INFO_NONE, NULL, NULL);
  guint64 mtime = 0;
  if (info) {
    GDateTime *dt = g_file_info_get_modification_date_time(info);
    if (dt) {
      mtime = (guint64)g_date_time_to_unix(dt);
      g_date_time_unref(dt);
    }
    g_object_unref(info);
  }
  g_object_unref(f);
  return mtime;
}

static gint compare_files_by_mtime(gconstpointer a, gconstpointer b) {
  const char *pa = *(const char **)a;
  const char *pb = *(const char **)b;
  guint64 ma = get_file_mtime(pa);
  guint64 mb = get_file_mtime(pb);
  if (ma < mb)
    return -1;
  if (ma > mb)
    return 1;
  return 0;
}

static void on_category_header_right_click(GtkGestureClick *gesture,
                                           G_GNUC_UNUSED int n_press, double x,
                                           double y, gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  GtkWidget *header =
      gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
  const char *cat_path = g_object_get_data(G_OBJECT(header), "cat-path");
  const char *cat_name = g_object_get_data(G_OBJECT(header), "cat-name");
  if (!cat_path)
    return;

  g_free(self->ctx_cat_path);
  self->ctx_cat_path = g_strdup(cat_path);
  g_free(self->ctx_cat_name);
  self->ctx_cat_name = g_strdup(cat_name);

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  GtkWidget *rename_btn = ctx_add_button(
      GTK_BOX(box), "Rename", G_CALLBACK(on_ctx_action_clicked), self);
  g_object_set_data(G_OBJECT(rename_btn), "action-name", "ctx-rename-category");
  GtkWidget *del_cat_btn = ctx_add_button(
      GTK_BOX(box), "Delete", G_CALLBACK(on_ctx_action_clicked), self);
  g_object_set_data(G_OBJECT(del_cat_btn), "action-name",
                    "ctx-delete-category");
  show_context_popover(header, box, x, y);
}

static void load_category(MoeMojiWindow *self, const char *kaomoji_dir,
                          const char *dirname) {
  char *cat_path = g_build_filename(kaomoji_dir, dirname, NULL);
  GDir *dir = g_dir_open(cat_path, 0, NULL);
  if (!dir) {
    g_free(cat_path);
    return;
  }
  char *display_name = get_category_display_name(cat_path);
  GtkWidget *header = gtk_label_new(display_name);
  gtk_widget_add_css_class(header, "category-header");
  gtk_label_set_xalign(GTK_LABEL(header), 0.0);
  g_object_set_data_full(G_OBJECT(header), "cat-path", g_strdup(cat_path),
                         g_free);
  g_object_set_data_full(G_OBJECT(header), "cat-name", g_strdup(display_name),
                         g_free);
  GtkGestureClick *cat_right_click = GTK_GESTURE_CLICK(gtk_gesture_click_new());
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(cat_right_click), 3);
  g_signal_connect(cat_right_click, "pressed",
                   G_CALLBACK(on_category_header_right_click), self);
  gtk_widget_add_controller(header, GTK_EVENT_CONTROLLER(cat_right_click));
  gtk_box_append(self->content_box, header);
  GtkWidget *flow = gtk_flow_box_new();
  gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(flow), TRUE);
  gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(flow), 2);
  gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flow), 4);
  gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow), GTK_SELECTION_NONE);
  GPtrArray *files = g_ptr_array_new_with_free_func(g_free);
  const char *filename;
  while ((filename = g_dir_read_name(dir)) != NULL) {
    if (!g_str_has_suffix(filename, ".txt"))
      continue;
    g_ptr_array_add(files, g_build_filename(cat_path, filename, NULL));
  }
  g_ptr_array_sort(files, compare_files_by_mtime);
  for (guint i = 0; i < files->len; i++) {
    const char *filepath = g_ptr_array_index(files, i);
    char *contents = NULL;
    if (g_file_get_contents(filepath, &contents, NULL, NULL)) {
      g_strchomp(contents);
      if (contents[0] != '\0')
        add_kaomoji_button(self, GTK_FLOW_BOX(flow), contents, filepath);
      g_free(contents);
    }
  }
  g_ptr_array_free(files, TRUE);
  gtk_box_append(self->content_box, flow);
  CategoryWidgets *cw = g_new0(CategoryWidgets, 1);
  cw->header = header;
  cw->flow = flow;
  cw->name = display_name;
  cw->path = g_strdup(cat_path);
  g_ptr_array_add(self->category_widgets, cw);
  g_dir_close(dir);
  g_free(cat_path);
}

static void set_active_chip(MoeMojiWindow *self, int index) {
  if (index == self->active_chip_index)
    return;
  if (self->active_chip_index >= 0 &&
      (guint)self->active_chip_index < self->category_widgets->len) {
    CategoryWidgets *old =
        g_ptr_array_index(self->category_widgets, self->active_chip_index);
    if (old->chip)
      gtk_widget_remove_css_class(old->chip, "chip-active");
  }
  self->active_chip_index = index;
  if (index >= 0 && (guint)index < self->category_widgets->len) {
    CategoryWidgets *cw = g_ptr_array_index(self->category_widgets, index);
    if (cw->chip)
      gtk_widget_add_css_class(cw->chip, "chip-active");
  }
}

static void scroll_chip_into_view(G_GNUC_UNUSED MoeMojiWindow *self,
                                  GtkWidget *chip) {
  GtkWidget *sw = gtk_widget_get_ancestor(chip, GTK_TYPE_SCROLLED_WINDOW);
  if (!sw)
    return;
  graphene_point_t p;
  GtkWidget *content = gtk_scrolled_window_get_child(GTK_SCROLLED_WINDOW(sw));
  if (gtk_widget_compute_point(chip, content, &GRAPHENE_POINT_INIT(0, 0), &p)) {
    GtkAdjustment *adj =
        gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(sw));
    double page = gtk_adjustment_get_page_size(adj);
    double val = gtk_adjustment_get_value(adj);
    int h = gtk_widget_get_height(chip);
    if (p.y < val)
      gtk_adjustment_set_value(adj, p.y);
    else if (p.y + h > val + page)
      gtk_adjustment_set_value(adj, p.y + h - page);
  }
}

static void scroll_to_category(MoeMojiWindow *self, int index) {
  CategoryWidgets *cw = g_ptr_array_index(self->category_widgets, index);
  graphene_point_t p;
  if (gtk_widget_compute_point(cw->header, GTK_WIDGET(self->content_box),
                               &GRAPHENE_POINT_INIT(0, 0), &p)) {
    GtkAdjustment *vadj = gtk_scrolled_window_get_vadjustment(
        GTK_SCROLLED_WINDOW(self->kaomoji_scroll));
    gtk_adjustment_set_value(vadj, p.y);
  }
  scroll_chip_into_view(self, cw->chip);
}

static void on_chip_clicked(GtkButton *button, gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  for (guint i = 0; i < self->category_widgets->len; i++) {
    CategoryWidgets *cw = g_ptr_array_index(self->category_widgets, i);
    if (cw->chip == GTK_WIDGET(button)) {
      set_active_chip(self, (int)i);
      graphene_point_t p;
      if (gtk_widget_compute_point(cw->header, GTK_WIDGET(self->content_box),
                                   &GRAPHENE_POINT_INIT(0, 0), &p)) {
        GtkAdjustment *vadj = gtk_scrolled_window_get_vadjustment(
            GTK_SCROLLED_WINDOW(self->kaomoji_scroll));
        gtk_adjustment_set_value(vadj, p.y);
      }
      return;
    }
  }
}

static void update_chip_from_scroll(MoeMojiWindow *self) {
  double scroll_pos =
      gtk_adjustment_get_value(gtk_scrolled_window_get_vadjustment(
          GTK_SCROLLED_WINDOW(self->kaomoji_scroll)));
  int best = 0;
  for (guint i = 0; i < self->category_widgets->len; i++) {
    CategoryWidgets *cw = g_ptr_array_index(self->category_widgets, i);
    graphene_point_t p;
    if (gtk_widget_compute_point(cw->header, GTK_WIDGET(self->content_box),
                                 &GRAPHENE_POINT_INIT(0, 0), &p)) {
      if (p.y <= scroll_pos)
        best = (int)i;
    }
  }
  set_active_chip(self, best);
}

static void update_spacer_height(MoeMojiWindow *self) {
  if (!self->bottom_spacer || self->category_widgets->len == 0)
    return;
  GtkAdjustment *vadj = gtk_scrolled_window_get_vadjustment(
      GTK_SCROLLED_WINDOW(self->kaomoji_scroll));
  int page = (int)gtk_adjustment_get_page_size(vadj);
  CategoryWidgets *last = g_ptr_array_index(self->category_widgets,
                                            self->category_widgets->len - 1);
  int spacing = gtk_box_get_spacing(self->content_box);
  int last_h = gtk_widget_get_height(last->header) + spacing +
               gtk_widget_get_height(last->flow);
  int spacer_h = page - last_h;
  if (spacer_h < 0)
    spacer_h = 0;
  gtk_widget_set_size_request(self->bottom_spacer, -1, spacer_h);
}

static void on_scroll_changed(G_GNUC_UNUSED GtkAdjustment *adj,
                              gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  update_chip_from_scroll(self);
}

static void on_page_size_changed(G_GNUC_UNUSED GtkAdjustment *adj,
                                 G_GNUC_UNUSED GParamSpec *pspec,
                                 gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  update_spacer_height(self);
}

static void on_kaomoji_scroll_map(G_GNUC_UNUSED GtkWidget *widget,
                                  gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  update_chip_from_scroll(self);
}

static void on_search_changed(GtkSearchEntry *entry, gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  const char *query = gtk_editable_get_text(GTK_EDITABLE(entry));
  if (!query || query[0] == '\0')
    return;
  gtk_widget_grab_focus(GTK_WIDGET(entry));
  g_autofree char *query_lower = g_utf8_strdown(query, -1);
  for (guint i = 0; i < self->category_widgets->len; i++) {
    CategoryWidgets *cw = g_ptr_array_index(self->category_widgets, i);
    g_autofree char *name_lower = g_utf8_strdown(cw->name, -1);
    if (strstr(name_lower, query_lower) != NULL) {
      scroll_to_category(self, (int)i);
      return;
    }
  }
}

static void collect_categories(const char *base_dir, GHashTable *seen,
                               GPtrArray *entries) {
  GDir *top = g_dir_open(base_dir, 0, NULL);
  if (!top)
    return;
  const char *name;
  while ((name = g_dir_read_name(top)) != NULL) {
    char *full = g_build_filename(base_dir, name, NULL);
    if (g_file_test(full, G_FILE_TEST_IS_DIR) &&
        !g_hash_table_contains(seen, name)) {
      g_hash_table_add(seen, g_strdup(name));
      char **pair = g_new(char *, 2);
      pair[0] = g_strdup(base_dir);
      pair[1] = g_strdup(name);
      g_ptr_array_add(entries, pair);
    }
    g_free(full);
  }
  g_dir_close(top);
}

static gint64 get_dir_time(const char *base, const char *name,
                           const char *attr) {
  g_autofree char *path = g_build_filename(base, name, NULL);
  GFile *file = g_file_new_for_path(path);
  g_autofree char *attrs =
      g_strconcat(attr, ",", G_FILE_ATTRIBUTE_TIME_MODIFIED, NULL);
  GFileInfo *info =
      g_file_query_info(file, attrs, G_FILE_QUERY_INFO_NONE, NULL, NULL);
  gint64 t = 0;
  if (info) {
    if (g_file_info_has_attribute(info, attr))
      t = g_file_info_get_attribute_uint64(info, attr);
    else
      t = g_file_info_get_attribute_uint64(info,
                                           G_FILE_ATTRIBUTE_TIME_MODIFIED);
    g_object_unref(info);
  }
  g_object_unref(file);
  return t;
}

static const char *current_sort_order = "alpha-asc";

static gint compare_entries(gconstpointer a, gconstpointer b) {
  char **ea = *(char ***)a;
  char **eb = *(char ***)b;
  const char *order = current_sort_order;
  gboolean desc = g_str_has_suffix(order, "-desc");

  if (g_str_has_prefix(order, "alpha")) {
    g_autofree char *pa = g_build_filename(ea[0], ea[1], NULL);
    g_autofree char *pb = g_build_filename(eb[0], eb[1], NULL);
    g_autofree char *da = get_category_display_name(pa);
    g_autofree char *db = get_category_display_name(pb);
    g_autofree char *ka = g_utf8_collate_key_for_filename(da, -1);
    g_autofree char *kb = g_utf8_collate_key_for_filename(db, -1);
    gint r = strcmp(ka, kb);
    return desc ? -r : r;
  }

  const char *attr = G_FILE_ATTRIBUTE_TIME_MODIFIED;
  gint64 ta = get_dir_time(ea[0], ea[1], attr);
  gint64 tb = get_dir_time(eb[0], eb[1], attr);
  gint r = (ta > tb) - (ta < tb);
  if (r == 0) {
    g_autofree char *ka = g_utf8_collate_key_for_filename(ea[1], -1);
    g_autofree char *kb = g_utf8_collate_key_for_filename(eb[1], -1);
    r = strcmp(ka, kb);
  }
  return desc ? -r : r;
}

static GPtrArray *collect_all_categories(void) {
  g_autofree char *user_dir = user_kaomoji_dir();
  GHashTable *seen =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  GPtrArray *entries = g_ptr_array_new();
  if (g_file_test(user_dir, G_FILE_TEST_IS_DIR))
    collect_categories(user_dir, seen, entries);
  g_hash_table_destroy(seen);
  g_ptr_array_sort(entries, compare_entries);
  return entries;
}

static void apply_saved_order(GSettings *settings, GPtrArray *entries) {
  g_auto(GStrv) saved = g_settings_get_strv(settings, "category-order");
  if (!saved || !saved[0])
    return;

  GPtrArray *ordered = g_ptr_array_new();
  gboolean *used = g_new0(gboolean, entries->len);

  for (int i = 0; saved[i]; i++) {
    for (guint j = 0; j < entries->len; j++) {
      if (used[j])
        continue;
      char **pair = g_ptr_array_index(entries, j);
      if (g_strcmp0(pair[1], saved[i]) == 0) {
        g_ptr_array_add(ordered, pair);
        used[j] = TRUE;
        break;
      }
    }
  }
  for (guint j = 0; j < entries->len; j++) {
    if (!used[j])
      g_ptr_array_add(ordered, g_ptr_array_index(entries, j));
  }
  for (guint i = 0; i < ordered->len; i++)
    g_ptr_array_index(entries, i) = g_ptr_array_index(ordered, i);
  g_ptr_array_free(ordered, TRUE);
  g_free(used);
}

static void free_category_entries(GPtrArray *entries) {
  for (guint i = 0; i < entries->len; i++) {
    char **pair = g_ptr_array_index(entries, i);
    g_free(pair[0]);
    g_free(pair[1]);
    g_free(pair);
  }
  g_ptr_array_free(entries, TRUE);
}

static void navigate_to(MoeMojiWindow *self, const char *page,
                        gboolean slide_left) {
  gtk_stack_set_transition_type(
      self->view_stack, slide_left ? GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT
                                   : GTK_STACK_TRANSITION_TYPE_SLIDE_RIGHT);
  gtk_stack_set_visible_child_name(self->view_stack, page);
  gboolean is_main = (g_strcmp0(page, "main") == 0);
  gtk_widget_set_visible(self->sidebar_toggle, is_main);
  gtk_widget_set_visible(self->add_button, is_main);
  gtk_widget_set_visible(self->sort_button, is_main);
  gtk_widget_set_visible(self->back_button, !is_main);
  gtk_widget_set_visible(self->menu_button, is_main);
}

static void on_add_clicked(G_GNUC_UNUSED GtkButton *button,
                           gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  navigate_to(self, "add-choice", TRUE);
}

static void on_back_clicked(G_GNUC_UNUSED GtkButton *button,
                            gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  navigate_to(self, "main", FALSE);
}

static void on_new_category_clicked(G_GNUC_UNUSED GtkButton *button,
                                    gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  gtk_editable_set_text(GTK_EDITABLE(self->category_name_entry), "");
  gtk_widget_set_sensitive(self->category_save_button, FALSE);
  navigate_to(self, "add-category", TRUE);
}

static void on_new_entry_clicked(G_GNUC_UNUSED GtkButton *button,
                                 gpointer user_data);

static gboolean is_valid_category_name(const char *text) {
  if (!text || text[0] == '\0')
    return FALSE;
  g_autofree char *stripped = g_strstrip(g_strdup(text));
  return stripped[0] != '\0';
}

static gboolean category_name_taken(const char *name) {
  g_autofree char *stripped = g_strstrip(g_strdup(name));
  GPtrArray *entries = collect_all_categories();
  gboolean found = FALSE;
  for (guint i = 0; i < entries->len; i++) {
    char **pair = g_ptr_array_index(entries, i);
    g_autofree char *cat_path = g_build_filename(pair[0], pair[1], NULL);
    g_autofree char *display = get_category_display_name(cat_path);
    if (g_utf8_collate(display, stripped) == 0) {
      found = TRUE;
      break;
    }
  }
  free_category_entries(entries);
  return found;
}

static void on_category_name_changed(G_GNUC_UNUSED GtkEditable *editable,
                                     gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  const char *text =
      gtk_editable_get_text(GTK_EDITABLE(self->category_name_entry));
  const char *error = NULL;

  if (is_valid_category_name(text) && category_name_taken(text))
    error = "A category with this name already exists";

  gboolean valid = is_valid_category_name(text) && !error;
  gtk_widget_set_sensitive(self->category_save_button, valid);
  gtk_label_set_text(GTK_LABEL(self->category_error_label), error ? error : "");
  gtk_widget_set_visible(self->category_error_label, error != NULL);
}

static void on_category_save_clicked(G_GNUC_UNUSED GtkButton *button,
                                     gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  const char *text =
      gtk_editable_get_text(GTK_EDITABLE(self->category_name_entry));
  if (!is_valid_category_name(text) || category_name_taken(text))
    return;
  g_autofree char *stripped = g_strstrip(g_strdup(text));
  g_autofree char *uuid = g_uuid_string_random();
  g_autofree char *udir = user_kaomoji_dir();
  g_autofree char *cat_path = g_build_filename(udir, uuid, NULL);
  g_mkdir_with_parents(cat_path, 0755);
  g_autofree char *name_file = g_build_filename(cat_path, ".name", NULL);
  g_file_set_contents(name_file, stripped, -1, NULL);

  reload_categories(self);
  navigate_to(self, "main", FALSE);
}

static void on_entry_category_picked(GtkButton *button, gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  const char *dir = g_object_get_data(G_OBJECT(button), "cat-dir");
  g_free(self->selected_category_dir);
  self->selected_category_dir = g_strdup(dir);
  GtkTextBuffer *buf =
      gtk_text_view_get_buffer(GTK_TEXT_VIEW(self->entry_text_view));
  gtk_text_buffer_set_text(buf, "", -1);
  navigate_to(self, "add-entry", TRUE);
}

static void build_pick_category_page(MoeMojiWindow *self,
                                     GtkWidget *pick_flow) {
  GtkWidget *child;
  while ((child = gtk_widget_get_first_child(pick_flow)) != NULL)
    gtk_flow_box_remove(GTK_FLOW_BOX(pick_flow), child);

  GPtrArray *entries = collect_all_categories();

  for (guint i = 0; i < entries->len; i++) {
    char **pair = g_ptr_array_index(entries, i);
    g_autofree char *full_path = g_build_filename(pair[0], pair[1], NULL);
    g_autofree char *display = get_category_display_name(full_path);
    GtkWidget *btn = gtk_button_new_with_label(display);
    gtk_widget_add_css_class(btn, "category-chip");
    gtk_widget_add_css_class(btn, "flat");
    g_object_set_data_full(G_OBJECT(btn), "cat-dir", g_strdup(full_path),
                           g_free);
    g_signal_connect(btn, "clicked", G_CALLBACK(on_entry_category_picked),
                     self);
    gtk_flow_box_insert(GTK_FLOW_BOX(pick_flow), btn, -1);
  }
  free_category_entries(entries);
}

static void on_entry_save_clicked(G_GNUC_UNUSED GtkButton *button,
                                  gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  if (!self->selected_category_dir)
    return;

  GtkTextBuffer *buf =
      gtk_text_view_get_buffer(GTK_TEXT_VIEW(self->entry_text_view));
  GtkTextIter start, end;
  gtk_text_buffer_get_bounds(buf, &start, &end);
  g_autofree char *text = gtk_text_buffer_get_text(buf, &start, &end, FALSE);
  g_strchomp(text);
  if (text[0] == '\0')
    return;
  int n = 1;
  while (TRUE) {
    g_autofree char *fname = g_strdup_printf("%d.txt", n);
    g_autofree char *fpath =
        g_build_filename(self->selected_category_dir, fname, NULL);
    if (!g_file_test(fpath, G_FILE_TEST_EXISTS)) {
      g_autofree char *with_newline = g_strdup_printf("%s\n", text);
      g_file_set_contents(fpath, with_newline, -1, NULL);
      break;
    }
    n++;
  }
  reload_categories(self);
  navigate_to(self, "main", FALSE);
}

static void save_category_order(MoeMojiWindow *self) {
  guint len = self->category_widgets->len;
  char **order = g_new0(char *, len + 1);
  for (guint i = 0; i < len; i++) {
    CategoryWidgets *cw = g_ptr_array_index(self->category_widgets, i);
    order[i] = g_path_get_basename(cw->path);
  }
  g_settings_set_strv(self->settings, "category-order",
                      (const char *const *)order);
  g_strfreev(order);
}

static GdkContentProvider *on_chip_drag_prepare(GtkDragSource *source,
                                                G_GNUC_UNUSED double x,
                                                G_GNUC_UNUSED double y,
                                                G_GNUC_UNUSED gpointer data) {
  GtkWidget *widget =
      gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(source));
  const char *path = g_object_get_data(G_OBJECT(widget), "cat-path");
  if (!path)
    return NULL;
  GValue val = G_VALUE_INIT;
  g_value_init(&val, G_TYPE_STRING);
  g_value_set_string(&val, path);
  return gdk_content_provider_new_for_value(&val);
}

static void on_chip_drag_begin(GtkDragSource *source,
                               G_GNUC_UNUSED GdkDrag *drag,
                               G_GNUC_UNUSED gpointer data) {
  GtkWidget *widget =
      gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(source));
  GdkPaintable *paintable = gtk_widget_paintable_new(widget);
  gtk_drag_source_set_icon(source, paintable, 0, 0);
  g_object_unref(paintable);
}

static void clear_drop_indicator(GtkWidget *chip_box) {
  GtkWidget *active = g_object_get_data(G_OBJECT(chip_box), "active-drop-sep");
  if (active) {
    gtk_widget_set_opacity(active, 0);
    g_object_set_data(G_OBJECT(chip_box), "active-drop-sep", NULL);
  }
}

static void show_drop_indicator(GtkWidget *chip, double y) {
  GtkWidget *chip_box = gtk_widget_get_parent(chip);
  if (!chip_box)
    return;
  clear_drop_indicator(chip_box);

  int height = gtk_widget_get_height(chip);
  GtkWidget *sep;
  if (y < height / 2.0)
    sep = g_object_get_data(G_OBJECT(chip), "drop-sep-before");
  else
    sep = g_object_get_data(G_OBJECT(chip), "drop-sep-after");
  if (sep) {
    gtk_widget_set_opacity(sep, 1);
    g_object_set_data(G_OBJECT(chip_box), "active-drop-sep", sep);
  }
}

static gboolean reorder_chips_idle(gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  if (self->category_widgets->len == 0)
    return G_SOURCE_REMOVE;
  CategoryWidgets *first = g_ptr_array_index(self->category_widgets, 0);
  GtkWidget *chip_box = gtk_widget_get_parent(first->chip);
  if (!chip_box)
    return G_SOURCE_REMOVE;
  GtkWidget *chip_after = NULL;
  for (guint i = 0; i < self->category_widgets->len; i++) {
    CategoryWidgets *cw = g_ptr_array_index(self->category_widgets, i);
    GtkWidget *sep = g_object_get_data(G_OBJECT(cw->chip), "drop-sep-before");
    if (sep) {
      gtk_widget_insert_after(sep, chip_box, chip_after);
      chip_after = sep;
    }
    gtk_widget_insert_after(cw->chip, chip_box, chip_after);
    chip_after = cw->chip;
  }
  CategoryWidgets *last_cw = g_ptr_array_index(self->category_widgets,
                                               self->category_widgets->len - 1);
  GtkWidget *sep_end =
      g_object_get_data(G_OBJECT(last_cw->chip), "drop-sep-after");
  if (sep_end)
    gtk_widget_insert_after(sep_end, chip_box, chip_after);
  return G_SOURCE_REMOVE;
}

static gboolean on_chip_drop(GtkDropTarget *target, const GValue *value,
                             G_GNUC_UNUSED double x, double y,
                             gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  GtkWidget *dst_widget =
      gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(target));
  const char *src_path = g_value_get_string(value);
  const char *dst_path = g_object_get_data(G_OBJECT(dst_widget), "cat-path");

  if (!src_path || !dst_path || g_strcmp0(src_path, dst_path) == 0)
    return FALSE;

  int src_idx = -1, dst_idx = -1;
  for (guint i = 0; i < self->category_widgets->len; i++) {
    CategoryWidgets *cw = g_ptr_array_index(self->category_widgets, i);
    if (g_strcmp0(cw->path, src_path) == 0)
      src_idx = (int)i;
    if (g_strcmp0(cw->path, dst_path) == 0)
      dst_idx = (int)i;
  }
  if (src_idx < 0 || dst_idx < 0)
    return FALSE;

  int height = gtk_widget_get_height(dst_widget);
  int insert_idx = (y < height / 2.0) ? dst_idx : dst_idx + 1;
  if (src_idx < insert_idx)
    insert_idx--;
  if (insert_idx == src_idx)
    return FALSE;

  CategoryWidgets *moved = g_ptr_array_index(self->category_widgets, src_idx);
  GPtrArray *reordered = g_ptr_array_new();
  for (guint i = 0; i < self->category_widgets->len; i++) {
    if ((int)i == src_idx)
      continue;
    if ((int)reordered->len == insert_idx)
      g_ptr_array_add(reordered, moved);
    g_ptr_array_add(reordered, g_ptr_array_index(self->category_widgets, i));
  }
  if ((int)reordered->len == insert_idx)
    g_ptr_array_add(reordered, moved);

  for (guint i = 0; i < reordered->len; i++)
    g_ptr_array_index(self->category_widgets, i) =
        g_ptr_array_index(reordered, i);
  g_ptr_array_free(reordered, TRUE);

  save_category_order(self);

  GtkWidget *after = NULL;
  for (guint i = 0; i < self->category_widgets->len; i++) {
    CategoryWidgets *cw = g_ptr_array_index(self->category_widgets, i);
    gtk_widget_insert_after(cw->header, GTK_WIDGET(self->content_box), after);
    gtk_widget_insert_after(cw->flow, GTK_WIDGET(self->content_box),
                            cw->header);
    after = cw->flow;
  }
  if (self->bottom_spacer)
    gtk_widget_insert_after(self->bottom_spacer, GTK_WIDGET(self->content_box),
                            after);

  GtkWidget *chip_box = gtk_widget_get_parent(dst_widget);
  if (chip_box)
    clear_drop_indicator(chip_box);

  for (guint i = 0; i < self->category_widgets->len; i++) {
    CategoryWidgets *cw = g_ptr_array_index(self->category_widgets, i);
    if ((int)i == self->active_chip_index)
      gtk_widget_add_css_class(cw->chip, "chip-active");
    else
      gtk_widget_remove_css_class(cw->chip, "chip-active");
  }

  g_idle_add(reorder_chips_idle, self);

  return TRUE;
}

static GdkDragAction on_chip_drag_hover(GtkDropTarget *target,
                                        G_GNUC_UNUSED double x, double y,
                                        G_GNUC_UNUSED gpointer data) {
  GtkWidget *widget =
      gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(target));
  show_drop_indicator(widget, y);
  return GDK_ACTION_MOVE;
}

static void on_chip_drag_leave(GtkDropTarget *target,
                               G_GNUC_UNUSED gpointer data) {
  GtkWidget *widget =
      gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(target));
  GtkWidget *chip_box = gtk_widget_get_parent(widget);
  if (chip_box)
    clear_drop_indicator(chip_box);
}

static void rebuild_pinned_box(MoeMojiWindow *self) {
  if (!self->pinned_box)
    return;
  GtkWidget *child;
  while ((child = gtk_widget_get_first_child(GTK_WIDGET(self->pinned_box))) !=
         NULL)
    gtk_box_remove(self->pinned_box, child);

  g_auto(GStrv) pinned = g_settings_get_strv(self->settings, "pinned-kaomojis");
  if (!pinned || !pinned[0]) {
    gtk_widget_set_visible(GTK_WIDGET(self->pinned_box), FALSE);
    return;
  }
  gtk_widget_set_visible(GTK_WIDGET(self->pinned_box), TRUE);

  GtkWidget *header = gtk_label_new("Pinned");
  gtk_widget_add_css_class(header, "pinned-header");
  gtk_label_set_xalign(GTK_LABEL(header), 0.0);
  gtk_box_append(self->pinned_box, header);

  for (guint i = 0; pinned[i]; i++) {
    const char *text = pinned[i];
    gboolean multiline = (strchr(text, '\n') != NULL);
    g_autofree char *first_line = NULL;
    if (multiline) {
      const char *nl = strchr(text, '\n');
      first_line = g_strndup(text, nl - text);
    }
    GtkWidget *btn = make_chip_button(multiline ? first_line : text);
    if (multiline)
      g_object_set_data_full(G_OBJECT(btn), "full-text", g_strdup(text),
                             g_free);
    g_signal_connect(btn, "clicked", G_CALLBACK(on_kaomoji_clicked), self);

    GtkGestureClick *rc = GTK_GESTURE_CLICK(gtk_gesture_click_new());
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rc), 3);
    g_signal_connect(rc, "pressed", G_CALLBACK(on_pinned_right_click), self);
    gtk_widget_add_controller(btn, GTK_EVENT_CONTROLLER(rc));

    gtk_box_append(self->pinned_box, btn);
  }
}

static void reload_categories(MoeMojiWindow *self) {
  GtkWidget *child;
  while ((child = gtk_widget_get_first_child(GTK_WIDGET(self->content_box))) !=
         NULL)
    gtk_box_remove(self->content_box, child);
  g_ptr_array_set_size(self->category_widgets, 0);
  self->active_chip_index = -1;
  self->bottom_spacer = NULL;

  if (self->category_bar) {
    gtk_paned_set_start_child(self->paned, NULL);
    self->category_bar = NULL;
  }
  gtk_widget_set_visible(self->sidebar_toggle, FALSE);
  GPtrArray *entries = collect_all_categories();
  apply_saved_order(self->settings, entries);

  if (entries->len == 0) {
    GtkWidget *label = gtk_label_new("No kaomoji data found.");
    gtk_box_append(self->content_box, label);
    free_category_entries(entries);
    return;
  }
  for (guint i = 0; i < entries->len; i++) {
    char **pair = g_ptr_array_index(entries, i);
    load_category(self, pair[0], pair[1]);
  }
  free_category_entries(entries);
  self->bottom_spacer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_append(self->content_box, self->bottom_spacer);
  if (self->category_widgets->len > 0) {
    GtkWidget *chip_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    for (guint i = 0; i < self->category_widgets->len; i++) {
      CategoryWidgets *cw = g_ptr_array_index(self->category_widgets, i);
      GtkWidget *btn = make_chip_button(cw->name);
      g_object_set_data_full(G_OBJECT(btn), "cat-path", g_strdup(cw->path),
                             g_free);
      g_object_set_data_full(G_OBJECT(btn), "cat-name", g_strdup(cw->name),
                             g_free);
      GtkGestureClick *rc = GTK_GESTURE_CLICK(gtk_gesture_click_new());
      gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rc), 3);
      g_signal_connect(rc, "pressed",
                       G_CALLBACK(on_category_header_right_click), self);
      gtk_widget_add_controller(btn, GTK_EVENT_CONTROLLER(rc));
      g_signal_connect(btn, "clicked", G_CALLBACK(on_chip_clicked), self);

      GtkDragSource *src = gtk_drag_source_new();
      gtk_drag_source_set_actions(src, GDK_ACTION_MOVE);
      g_signal_connect(src, "prepare", G_CALLBACK(on_chip_drag_prepare), self);
      g_signal_connect(src, "drag-begin", G_CALLBACK(on_chip_drag_begin), self);
      gtk_widget_add_controller(btn, GTK_EVENT_CONTROLLER(src));

      GtkDropTarget *tgt = gtk_drop_target_new(G_TYPE_STRING, GDK_ACTION_MOVE);
      g_signal_connect(tgt, "drop", G_CALLBACK(on_chip_drop), self);
      g_signal_connect(tgt, "enter", G_CALLBACK(on_chip_drag_hover), self);
      g_signal_connect(tgt, "motion", G_CALLBACK(on_chip_drag_hover), self);
      g_signal_connect(tgt, "leave", G_CALLBACK(on_chip_drag_leave), self);
      gtk_widget_add_controller(btn, GTK_EVENT_CONTROLLER(tgt));

      cw->chip = btn;

      GtkWidget *sep = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
      gtk_widget_add_css_class(sep, "chip-drop-line");
      gtk_widget_set_size_request(sep, -1, 2);
      gtk_widget_set_opacity(sep, 0);
      gtk_widget_set_can_target(sep, FALSE);
      gtk_box_append(GTK_BOX(chip_box), sep);
      g_object_set_data(G_OBJECT(btn), "drop-sep-before", sep);
      if (i > 0) {
        CategoryWidgets *prev_cw =
            g_ptr_array_index(self->category_widgets, i - 1);
        g_object_set_data(G_OBJECT(prev_cw->chip), "drop-sep-after", sep);
      }

      gtk_box_append(GTK_BOX(chip_box), btn);

      if (i == self->category_widgets->len - 1) {
        GtkWidget *tail = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_add_css_class(tail, "chip-drop-line");
        gtk_widget_set_size_request(tail, -1, 2);
        gtk_widget_set_opacity(tail, 0);
        gtk_widget_set_can_target(tail, FALSE);
        gtk_box_append(GTK_BOX(chip_box), tail);
        g_object_set_data(G_OBJECT(btn), "drop-sep-after", tail);
      }
    }
    self->category_bar = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
    gtk_widget_add_css_class(GTK_WIDGET(self->category_bar), "category-bar");
    GtkWidget *cat_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(cat_scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(cat_scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(cat_scroll), chip_box);
    gtk_box_append(self->category_bar, cat_scroll);

    self->pinned_box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 2));
    gtk_widget_add_css_class(GTK_WIDGET(self->pinned_box), "pinned-section");
    gtk_box_append(self->category_bar, GTK_WIDGET(self->pinned_box));
    rebuild_pinned_box(self);

    gtk_paned_set_start_child(self->paned, GTK_WIDGET(self->category_bar));
    gtk_widget_set_visible(self->sidebar_toggle, TRUE);
    gtk_widget_set_visible(
        GTK_WIDGET(self->category_bar),
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(self->sidebar_toggle)));
    GtkAdjustment *vadj = gtk_scrolled_window_get_vadjustment(
        GTK_SCROLLED_WINDOW(self->kaomoji_scroll));
    if (self->scroll_handler_id != 0)
      g_signal_handler_disconnect(vadj, self->scroll_handler_id);
    if (self->page_size_handler_id != 0)
      g_signal_handler_disconnect(vadj, self->page_size_handler_id);
    self->scroll_handler_id = g_signal_connect(
        vadj, "value-changed", G_CALLBACK(on_scroll_changed), self);
    self->page_size_handler_id = g_signal_connect(
        vadj, "notify::page-size", G_CALLBACK(on_page_size_changed), self);
  }
}

static void on_new_entry_clicked(G_GNUC_UNUSED GtkButton *button,
                                 gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  GtkWidget *pick_page =
      gtk_stack_get_child_by_name(self->view_stack, "pick-category");
  GtkWidget *pick_flow = g_object_get_data(G_OBJECT(pick_page), "pick-flow");
  build_pick_category_page(self, pick_flow);
  navigate_to(self, "pick-category", TRUE);
}

static GtkWidget *build_add_choice_page(MoeMojiWindow *self) {
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
  gtk_widget_add_css_class(box, "add-form");
  gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
  gtk_widget_set_vexpand(box, TRUE);

  GtkWidget *cat_btn = gtk_button_new_with_label("New category!");
  gtk_widget_add_css_class(cat_btn, "add-choice-button");
  g_signal_connect(cat_btn, "clicked", G_CALLBACK(on_new_category_clicked),
                   self);
  gtk_box_append(GTK_BOX(box), cat_btn);
  GtkWidget *entry_btn = gtk_button_new_with_label("New emote!");
  gtk_widget_add_css_class(entry_btn, "add-choice-button");
  g_signal_connect(entry_btn, "clicked", G_CALLBACK(on_new_entry_clicked),
                   self);
  gtk_box_append(GTK_BOX(box), entry_btn);

  return box;
}

static GtkWidget *build_add_category_page(MoeMojiWindow *self) {
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_add_css_class(box, "add-form");
  gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
  gtk_widget_set_vexpand(box, TRUE);
  gtk_widget_set_size_request(box, 300, -1);
  GtkWidget *label = gtk_label_new("Name your category!");
  gtk_widget_add_css_class(label, "category-header");
  gtk_box_append(GTK_BOX(box), label);
  self->category_name_entry = GTK_ENTRY(gtk_entry_new());
  gtk_entry_set_placeholder_text(self->category_name_entry,
                                 "Something like 'cute'");
  gtk_box_append(GTK_BOX(box), GTK_WIDGET(self->category_name_entry));
  g_signal_connect(self->category_name_entry, "changed",
                   G_CALLBACK(on_category_name_changed), self);

  self->category_error_label = gtk_label_new("");
  gtk_widget_add_css_class(self->category_error_label, "error-label");
  gtk_widget_set_visible(self->category_error_label, FALSE);
  gtk_box_append(GTK_BOX(box), self->category_error_label);

  self->category_save_button = gtk_button_new_with_label("Save");
  gtk_widget_set_sensitive(self->category_save_button, FALSE);
  gtk_widget_add_css_class(self->category_save_button, "suggested-action");
  g_signal_connect(self->category_save_button, "clicked",
                   G_CALLBACK(on_category_save_clicked), self);
  gtk_box_append(GTK_BOX(box), self->category_save_button);

  return box;
}

static GtkWidget *build_pick_category_page_widget(void) {
  GtkWidget *wrapper = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_vexpand(wrapper, TRUE);
  gtk_widget_set_halign(wrapper, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(wrapper, GTK_ALIGN_FILL);
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_add_css_class(box, "add-form");
  gtk_widget_set_vexpand(box, TRUE);
  gtk_widget_set_size_request(box, 400, -1);
  GtkWidget *clamp = adw_clamp_new();
  adw_clamp_set_maximum_size(ADW_CLAMP(clamp), 600);
  adw_clamp_set_tightening_threshold(ADW_CLAMP(clamp), 400);
  gtk_widget_set_vexpand(clamp, TRUE);

  GtkWidget *label = gtk_label_new("Pick a Category");
  gtk_widget_add_css_class(label, "category-header");
  gtk_box_append(GTK_BOX(box), label);

  GtkWidget *scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER,
                                 GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand(scroll, TRUE);

  GtkWidget *pick_flow = gtk_flow_box_new();
  gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(pick_flow), FALSE);
  gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(pick_flow), 20);
  gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(pick_flow), GTK_SELECTION_NONE);
  gtk_widget_add_css_class(pick_flow, "category-chips");
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), pick_flow);
  gtk_box_append(GTK_BOX(box), scroll);

  adw_clamp_set_child(ADW_CLAMP(clamp), box);
  gtk_box_append(GTK_BOX(wrapper), clamp);
  g_object_set_data(G_OBJECT(wrapper), "pick-flow", pick_flow);

  return wrapper;
}

static GtkWidget *build_add_entry_page(MoeMojiWindow *self) {
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_add_css_class(box, "add-form");
  gtk_widget_set_vexpand(box, TRUE);

  GtkWidget *label = gtk_label_new("New emote");
  gtk_widget_add_css_class(label, "category-header");
  gtk_box_append(GTK_BOX(box), label);

  GtkWidget *scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER,
                                 GTK_POLICY_AUTOMATIC);
  gtk_widget_add_css_class(scroll, "entry-editor-frame");
  gtk_widget_set_vexpand(scroll, TRUE);
  gtk_widget_set_size_request(scroll, -1, 100);

  self->entry_text_view = gtk_text_view_new();
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(self->entry_text_view),
                              GTK_WRAP_CHAR);
  gtk_text_view_set_left_margin(GTK_TEXT_VIEW(self->entry_text_view), 8);
  gtk_text_view_set_right_margin(GTK_TEXT_VIEW(self->entry_text_view), 8);
  gtk_text_view_set_top_margin(GTK_TEXT_VIEW(self->entry_text_view), 8);
  gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(self->entry_text_view), 8);
  gtk_widget_add_css_class(self->entry_text_view, "entry-text-view");
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll),
                                self->entry_text_view);
  gtk_box_append(GTK_BOX(box), scroll);

  GtkWidget *save_btn = gtk_button_new_with_label("Save");
  gtk_widget_add_css_class(save_btn, "suggested-action");
  g_signal_connect(save_btn, "clicked", G_CALLBACK(on_entry_save_clicked),
                   self);
  gtk_box_append(GTK_BOX(box), save_btn);

  return box;
}

static void remove_dir_recursive(const char *path) {
  GDir *dir = g_dir_open(path, 0, NULL);
  if (!dir)
    return;
  const char *name;
  while ((name = g_dir_read_name(dir)) != NULL) {
    g_autofree char *child = g_build_filename(path, name, NULL);
    if (g_file_test(child, G_FILE_TEST_IS_DIR))
      remove_dir_recursive(child);
    else
      g_unlink(child);
  }
  g_dir_close(dir);
  g_rmdir(path);
}

static void copy_dir_recursive(const char *src, const char *dst) {
  g_mkdir_with_parents(dst, 0755);
  GDir *dir = g_dir_open(src, 0, NULL);
  if (!dir)
    return;
  const char *name;
  while ((name = g_dir_read_name(dir)) != NULL) {
    g_autofree char *s = g_build_filename(src, name, NULL);
    g_autofree char *d = g_build_filename(dst, name, NULL);
    if (g_file_test(s, G_FILE_TEST_IS_DIR)) {
      copy_dir_recursive(s, d);
    } else {
      char *contents = NULL;
      gsize len = 0;
      if (g_file_get_contents(s, &contents, &len, NULL)) {
        g_file_set_contents(d, contents, len, NULL);
        g_free(contents);
      }
    }
  }
  g_dir_close(dir);
}

static void on_manage_delete_emote_response(AdwAlertDialog *dialog,
                                            GAsyncResult *res,
                                            gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  const char *response = adw_alert_dialog_choose_finish(dialog, res);
  if (g_strcmp0(response, "yes") == 0) {
    const char *filepath = g_object_get_data(G_OBJECT(dialog), "emote-path");
    const char *text = g_object_get_data(G_OBJECT(dialog), "emote-text");
    g_unlink(filepath);
    if (text && is_kaomoji_pinned(self, text)) {
      g_auto(GStrv) pinned =
          g_settings_get_strv(self->settings, "pinned-kaomojis");
      GPtrArray *arr = g_ptr_array_new();
      for (guint i = 0; pinned[i]; i++) {
        if (g_strcmp0(pinned[i], text) != 0)
          g_ptr_array_add(arr, pinned[i]);
      }
      g_ptr_array_add(arr, NULL);
      g_settings_set_strv(self->settings, "pinned-kaomojis",
                          (const char *const *)arr->pdata);
      g_ptr_array_free(arr, TRUE);
    }
    reload_categories(self);
  }
}

static void on_ctx_delete_emote(G_GNUC_UNUSED GSimpleAction *action,
                                G_GNUC_UNUSED GVariant *parameter,
                                gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  const char *filepath = self->ctx_emote_path;
  const char *emote_text = self->ctx_emote_text;

  g_autofree char *body = NULL;
  if (g_utf8_strlen(emote_text, -1) > 40) {
    char *end = g_utf8_offset_to_pointer(emote_text, 40);
    g_autofree char *prefix = g_strndup(emote_text, end - emote_text);
    body = g_strdup_printf("%s…", prefix);
  } else {
    body = g_strdup(emote_text);
  }
  AdwAlertDialog *confirm =
      ADW_ALERT_DIALOG(adw_alert_dialog_new("Delete emote?", body));
  adw_alert_dialog_add_responses(confirm, "no", "No", "yes", "Yes", NULL);
  adw_alert_dialog_set_response_appearance(confirm, "yes",
                                           ADW_RESPONSE_DESTRUCTIVE);
  adw_alert_dialog_set_default_response(confirm, "no");
  adw_alert_dialog_set_close_response(confirm, "no");
  g_object_set_data_full(G_OBJECT(confirm), "emote-path", g_strdup(filepath),
                         g_free);
  g_object_set_data_full(G_OBJECT(confirm), "emote-text", g_strdup(emote_text),
                         g_free);

  adw_alert_dialog_choose(confirm, GTK_WIDGET(self), NULL,
                          (GAsyncReadyCallback)on_manage_delete_emote_response,
                          self);
}

static void on_manage_delete_category_response(AdwAlertDialog *dialog,
                                               GAsyncResult *res,
                                               gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  const char *response = adw_alert_dialog_choose_finish(dialog, res);
  if (g_strcmp0(response, "yes") == 0) {
    const char *cat_path = g_object_get_data(G_OBJECT(dialog), "cat-path");
    remove_dir_recursive(cat_path);
    reload_categories(self);
  }
}

static void on_rename_entry_changed(GtkEditable *editable, gpointer user_data) {
  AdwAlertDialog *dialog = ADW_ALERT_DIALOG(user_data);
  const char *text = gtk_editable_get_text(editable);
  const char *original = g_object_get_data(G_OBJECT(dialog), "original-name");
  g_autofree char *stripped = g_strstrip(g_strdup(text));
  const char *error = NULL;

  if (stripped[0] == '\0')
    error = "Name cannot be empty";
  else if (category_name_taken(stripped))
    error = "A category with this name already exists";

  gboolean valid = !error && g_strcmp0(stripped, original) != 0;
  adw_alert_dialog_set_body(dialog, error ? error : "");
  adw_alert_dialog_set_response_enabled(dialog, "rename", valid);
}

static void on_rename_entry_activate(G_GNUC_UNUSED GtkEntry *entry,
                                     gpointer user_data) {
  AdwAlertDialog *dialog = ADW_ALERT_DIALOG(user_data);
  if (adw_alert_dialog_get_response_enabled(dialog, "rename")) {
    g_signal_emit_by_name(dialog, "response", "rename");
    adw_dialog_force_close(ADW_DIALOG(dialog));
  }
}

static void on_rename_category_response(AdwAlertDialog *dialog,
                                        const char *response,
                                        gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  if (g_strcmp0(response, "rename") != 0)
    return;
  const char *cat_path = g_object_get_data(G_OBJECT(dialog), "cat-path");
  GtkEditable *entry =
      GTK_EDITABLE(g_object_get_data(G_OBJECT(dialog), "name-entry"));
  const char *raw = gtk_editable_get_text(entry);
  g_autofree char *stripped = g_strstrip(g_strdup(raw));
  if (stripped[0] == '\0')
    return;
  g_autofree char *name_file = g_build_filename(cat_path, ".name", NULL);
  g_file_set_contents(name_file, stripped, -1, NULL);
  for (guint i = 0; i < self->category_widgets->len; i++) {
    CategoryWidgets *cw = g_ptr_array_index(self->category_widgets, i);
    if (g_strcmp0(cw->path, cat_path) == 0) {
      g_free(cw->name);
      cw->name = g_strdup(stripped);
      gtk_label_set_text(GTK_LABEL(cw->header), stripped);
      if (cw->chip) {
        GtkWidget *chip_label = gtk_button_get_child(GTK_BUTTON(cw->chip));
        gtk_label_set_text(GTK_LABEL(chip_label), stripped);
        g_object_set_data_full(G_OBJECT(cw->chip), "cat-name",
                               g_strdup(stripped), g_free);
      }
      break;
    }
  }
}

static void on_ctx_rename_category(G_GNUC_UNUSED GSimpleAction *action,
                                   G_GNUC_UNUSED GVariant *parameter,
                                   gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  const char *cat_path = self->ctx_cat_path;
  const char *cat_name = self->ctx_cat_name;

  AdwAlertDialog *dialog =
      ADW_ALERT_DIALOG(adw_alert_dialog_new("Rename Category", NULL));
  adw_alert_dialog_add_responses(dialog, "cancel", "Cancel", "rename", "Rename",
                                 NULL);
  adw_alert_dialog_set_response_appearance(dialog, "rename",
                                           ADW_RESPONSE_SUGGESTED);
  adw_alert_dialog_set_default_response(dialog, "rename");
  adw_alert_dialog_set_close_response(dialog, "cancel");
  adw_alert_dialog_set_response_enabled(dialog, "rename", FALSE);

  GtkWidget *entry = gtk_entry_new();
  gtk_editable_set_text(GTK_EDITABLE(entry), cat_name);
  adw_alert_dialog_set_extra_child(dialog, entry);
  g_signal_connect_swapped(dialog, "map", G_CALLBACK(gtk_widget_grab_focus),
                           entry);
  g_signal_connect(entry, "activate", G_CALLBACK(on_rename_entry_activate),
                   dialog);
  g_signal_connect(entry, "changed", G_CALLBACK(on_rename_entry_changed),
                   dialog);
  g_object_set_data_full(G_OBJECT(dialog), "cat-path", g_strdup(cat_path),
                         g_free);
  g_object_set_data_full(G_OBJECT(dialog), "original-name", g_strdup(cat_name),
                         g_free);
  g_object_set_data(G_OBJECT(dialog), "name-entry", entry);
  g_signal_connect(dialog, "response", G_CALLBACK(on_rename_category_response),
                   self);
  adw_alert_dialog_choose(dialog, GTK_WIDGET(self), NULL, NULL, NULL);
}

static void on_ctx_delete_category(G_GNUC_UNUSED GSimpleAction *action,
                                   G_GNUC_UNUSED GVariant *parameter,
                                   gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  const char *cat_path = self->ctx_cat_path;
  const char *cat_name = self->ctx_cat_name;

  g_autofree char *heading = g_strdup_printf("Delete \"%s\"?", cat_name);
  AdwAlertDialog *confirm = ADW_ALERT_DIALOG(adw_alert_dialog_new(
      heading, "Are you sure? This will erase all emotes in this category."));
  adw_alert_dialog_add_responses(confirm, "no", "No", "yes", "Yes", NULL);
  adw_alert_dialog_set_response_appearance(confirm, "yes",
                                           ADW_RESPONSE_DESTRUCTIVE);
  adw_alert_dialog_set_default_response(confirm, "no");
  adw_alert_dialog_set_close_response(confirm, "no");
  g_object_set_data_full(G_OBJECT(confirm), "cat-path", g_strdup(cat_path),
                         g_free);

  adw_alert_dialog_choose(
      confirm, GTK_WIDGET(self), NULL,
      (GAsyncReadyCallback)on_manage_delete_category_response, self);
}

static void on_export_save_ready(GObject *source, GAsyncResult *res,
                                 G_GNUC_UNUSED gpointer user_data) {
  GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
  GFile *file = gtk_file_dialog_save_finish(dialog, res, NULL);
  if (!file)
    return;

  g_autofree char *path = g_file_get_path(file);
  g_object_unref(file);

  g_autofree char *tmpdir = g_dir_make_tmp("moemoji-export-XXXXXX", NULL);
  if (!tmpdir)
    return;
  g_autofree char *user_dir = user_kaomoji_dir();
  if (g_file_test(user_dir, G_FILE_TEST_IS_DIR)) {
    GDir *dir = g_dir_open(user_dir, 0, NULL);
    if (dir) {
      const char *name;
      while ((name = g_dir_read_name(dir)) != NULL) {
        g_autofree char *src = g_build_filename(user_dir, name, NULL);
        if (g_file_test(src, G_FILE_TEST_IS_DIR)) {
          g_autofree char *dst = g_build_filename(tmpdir, name, NULL);
          copy_dir_recursive(src, dst);
        }
      }
      g_dir_close(dir);
    }
  }
  GSubprocess *tar = g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, NULL, "tar",
                                      "czf", path, "-C", tmpdir, ".", NULL);
  if (tar) {
    g_subprocess_wait(tar, NULL, NULL);
    g_object_unref(tar);
  }

  remove_dir_recursive(tmpdir);
}

static GtkFileDialog *new_targz_dialog(const char *title) {
  GtkFileDialog *dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, title);
  GtkFileFilter *filter = gtk_file_filter_new();
  gtk_file_filter_set_name(filter, "Tar archives (*.tar.gz)");
  gtk_file_filter_add_pattern(filter, "*.tar.gz");
  GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
  g_list_store_append(filters, filter);
  g_object_unref(filter);
  gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
  g_object_unref(filters);
  return dialog;
}

static void on_export_activated(G_GNUC_UNUSED GSimpleAction *action,
                                G_GNUC_UNUSED GVariant *parameter,
                                gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  GtkFileDialog *dialog = new_targz_dialog("Export Kaomojis");
  gtk_file_dialog_set_initial_name(dialog, "moemoji-export.tar.gz");
  gtk_file_dialog_save(dialog, GTK_WINDOW(self), NULL, on_export_save_ready,
                       self);
  g_object_unref(dialog);
}

static void clear_user_categories(void) {
  g_autofree char *user_dir = user_kaomoji_dir();
  GDir *dir = g_dir_open(user_dir, 0, NULL);
  if (!dir)
    return;
  const char *name;
  while ((name = g_dir_read_name(dir)) != NULL) {
    g_autofree char *child = g_build_filename(user_dir, name, NULL);
    if (g_file_test(child, G_FILE_TEST_IS_DIR))
      remove_dir_recursive(child);
  }
  g_dir_close(dir);
}

static void do_import(MoeMojiWindow *self, const char *fpath) {
  g_autofree char *user_dir = user_kaomoji_dir();
  clear_user_categories();
  g_mkdir_with_parents(user_dir, 0755);
  GSubprocess *tar = g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, NULL, "tar",
                                      "xzf", fpath, "-C", user_dir, NULL);
  if (tar) {
    g_subprocess_wait(tar, NULL, NULL);
    g_object_unref(tar);
  }
  reload_categories(self);
}

static void on_import_confirm_response(AdwAlertDialog *dialog,
                                       GAsyncResult *res, gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  const char *response = adw_alert_dialog_choose_finish(dialog, res);
  if (g_strcmp0(response, "import") == 0) {
    const char *path = g_object_get_data(G_OBJECT(dialog), "import-path");
    do_import(self, path);
  }
}

static void on_import_open_ready(GObject *source, GAsyncResult *res,
                                 gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
  GFile *file = gtk_file_dialog_open_finish(dialog, res, NULL);
  if (!file)
    return;
  char *fpath = g_file_get_path(file);
  g_object_unref(file);
  AdwAlertDialog *confirm = ADW_ALERT_DIALOG(adw_alert_dialog_new(
      "Import Kaomojis?",
      "Importing will erase your previous kaomojis. Proceed?"));
  adw_alert_dialog_add_responses(confirm, "cancel", "No", "import", "Yes",
                                 NULL);
  adw_alert_dialog_set_response_appearance(confirm, "import",
                                           ADW_RESPONSE_DESTRUCTIVE);
  adw_alert_dialog_set_default_response(confirm, "cancel");
  adw_alert_dialog_set_close_response(confirm, "cancel");
  g_object_set_data_full(G_OBJECT(confirm), "import-path", fpath, g_free);

  adw_alert_dialog_choose(confirm, GTK_WIDGET(self), NULL,
                          (GAsyncReadyCallback)on_import_confirm_response,
                          self);
}

static void on_import_activated(G_GNUC_UNUSED GSimpleAction *action,
                                G_GNUC_UNUSED GVariant *parameter,
                                gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  GtkFileDialog *dialog = new_targz_dialog("Import Kaomojis");
  gtk_file_dialog_open(dialog, GTK_WINDOW(self), NULL, on_import_open_ready,
                       self);
  g_object_unref(dialog);
}

static void update_sort_button_icon(MoeMojiWindow *self) {
  g_autofree char *order =
      g_settings_get_string(self->settings, "category-sort");
  const char *icon = g_str_has_suffix(order, "-desc")
                         ? "view-sort-descending-symbolic"
                         : "view-sort-ascending-symbolic";
  gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(self->sort_button), icon);
}

static void on_sort_order_changed(GSimpleAction *action, GVariant *parameter,
                                  gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  g_simple_action_set_state(action, parameter);
  const char *order = g_variant_get_string(parameter, NULL);
  g_settings_set_string(self->settings, "category-sort", order);
  current_sort_order = order;
  update_sort_button_icon(self);
  const char *empty[] = {NULL};
  g_settings_set_strv(self->settings, "category-order", empty);
  reload_categories(self);
  save_category_order(self);
}

static void on_restore_default_order(G_GNUC_UNUSED GSimpleAction *action,
                                     G_GNUC_UNUSED GVariant *parameter,
                                     gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  g_settings_reset(self->settings, "category-order");
  reload_categories(self);
}

static void on_reset_kaomojis_response(AdwAlertDialog *dialog,
                                       GAsyncResult *res, gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  const char *response = adw_alert_dialog_choose_finish(dialog, res);
  if (g_strcmp0(response, "reset") != 0)
    return;
  clear_user_categories();
  copy_builtins_to_user_dir();
  g_settings_reset(self->settings, "category-order");
  g_settings_reset(self->settings, "pinned-kaomojis");
  reload_categories(self);
}

static void on_reset_kaomojis(G_GNUC_UNUSED GSimpleAction *action,
                              G_GNUC_UNUSED GVariant *parameter,
                              gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  AdwAlertDialog *dialog = ADW_ALERT_DIALOG(adw_alert_dialog_new(
      "Reset Kaomojis?", "This will delete all custom categories and restore "
                         "the built-in kaomojis. Pinned kaomojis will also "
                         "be cleared."));
  adw_alert_dialog_add_responses(dialog, "cancel", "Cancel", "reset", "Reset",
                                 NULL);
  adw_alert_dialog_set_response_appearance(dialog, "reset",
                                           ADW_RESPONSE_DESTRUCTIVE);
  adw_alert_dialog_set_default_response(dialog, "cancel");
  adw_alert_dialog_set_close_response(dialog, "cancel");
  adw_alert_dialog_choose(dialog, GTK_WIDGET(self), NULL,
                          (GAsyncReadyCallback)on_reset_kaomojis_response,
                          self);
}

static void reset_chip_controllers(MoeMojiWindow *self) {
  for (guint i = 0; i < self->category_widgets->len; i++) {
    CategoryWidgets *cw = g_ptr_array_index(self->category_widgets, i);
    if (!cw->chip)
      continue;
    GListModel *controllers = gtk_widget_observe_controllers(cw->chip);
    guint n = g_list_model_get_n_items(controllers);
    for (guint j = 0; j < n; j++) {
      GtkEventController *c = g_list_model_get_item(controllers, j);
      if (GTK_IS_DRAG_SOURCE(c) || GTK_IS_DROP_TARGET(c))
        gtk_event_controller_reset(c);
      g_object_unref(c);
    }
    g_object_unref(controllers);
  }
}

static void on_sidebar_toggled(GtkToggleButton *toggle, gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  gboolean active = gtk_toggle_button_get_active(toggle);
  GtkWidget *start = gtk_paned_get_start_child(self->paned);
  if (start)
    gtk_widget_set_visible(start, active);
  if (active)
    reset_chip_controllers(self);
  gtk_button_set_icon_name(GTK_BUTTON(toggle),
                           active ? "pan-start-symbolic" : "pan-end-symbolic");
}

#define SIDEBAR_HIDE_WIDTH 450

static void on_surface_layout(G_GNUC_UNUSED GdkSurface *surface, int width,
                              G_GNUC_UNUSED int height, gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  if (width > 0 && width < SIDEBAR_HIDE_WIDTH)
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->sidebar_toggle),
                                 FALSE);
}

static void on_window_realize(GtkWidget *widget,
                              G_GNUC_UNUSED gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(widget);
  GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(self));
  if (surface)
    g_signal_connect(surface, "layout", G_CALLBACK(on_surface_layout), self);
}

static void moemoji_window_class_init(MoeMojiWindowClass *klass) {
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
  gtk_widget_class_set_template_from_resource(
      widget_class, "/jp/angeltech/MoeMoji/moemoji-window.ui");
  gtk_widget_class_bind_template_child(widget_class, MoeMojiWindow, paned);
  gtk_widget_class_bind_template_child(widget_class, MoeMojiWindow, right_pane);
  gtk_widget_class_bind_template_child(widget_class, MoeMojiWindow, outer_box);
  gtk_widget_class_bind_template_child(widget_class, MoeMojiWindow,
                                       content_box);
  gtk_widget_class_bind_template_child(widget_class, MoeMojiWindow,
                                       search_entry);
  gtk_widget_class_bind_template_child(widget_class, MoeMojiWindow,
                                       kaomoji_scroll);
  gtk_widget_class_bind_template_child(widget_class, MoeMojiWindow, view_stack);
  gtk_widget_class_bind_template_child(widget_class, MoeMojiWindow,
                                       toast_overlay);
}

static void moemoji_window_init(MoeMojiWindow *self) {
  first_launch_setup();
  gtk_widget_init_template(GTK_WIDGET(self));
  gtk_widget_add_css_class(GTK_WIDGET(self), "wallpaper-bg");
  gtk_widget_add_css_class(GTK_WIDGET(self->content_box), "content-area");
  gtk_paned_set_position(self->paned, 150);

  GtkWidget *empty_titlebar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_visible(empty_titlebar, FALSE);
  gtk_window_set_titlebar(GTK_WINDOW(self), empty_titlebar);

  self->header_bar = GTK_WIDGET(adw_header_bar_new());
  gtk_widget_add_css_class(self->header_bar, "flat");
  adw_header_bar_set_show_start_title_buttons(ADW_HEADER_BAR(self->header_bar),
                                              FALSE);
  adw_header_bar_set_show_end_title_buttons(ADW_HEADER_BAR(self->header_bar),
                                            TRUE);
  self->settings = g_settings_new("jp.angeltech.MoeMoji");

  self->sidebar_toggle = GTK_WIDGET(gtk_toggle_button_new());
  gtk_button_set_icon_name(GTK_BUTTON(self->sidebar_toggle),
                           "pan-start-symbolic");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->sidebar_toggle), TRUE);
  adw_header_bar_pack_start(ADW_HEADER_BAR(self->header_bar),
                            self->sidebar_toggle);

  self->add_button = gtk_button_new_from_icon_name("list-add-symbolic");
  adw_header_bar_pack_start(ADW_HEADER_BAR(self->header_bar), self->add_button);

  self->sort_button = GTK_WIDGET(gtk_menu_button_new());
  GMenu *sort_menu = g_menu_new();
  g_menu_append(sort_menu, "Alphabetically ↑", "win.sort-order::alpha-asc");
  g_menu_append(sort_menu, "Alphabetically ↓", "win.sort-order::alpha-desc");
  g_menu_append(sort_menu, "Modified ↑", "win.sort-order::modified-asc");
  g_menu_append(sort_menu, "Modified ↓", "win.sort-order::modified-desc");
  g_menu_append(sort_menu, "Default order", "win.restore-default-order");
  gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(self->sort_button),
                                 G_MENU_MODEL(sort_menu));
  g_object_unref(sort_menu);
  adw_header_bar_pack_start(ADW_HEADER_BAR(self->header_bar),
                            self->sort_button);

  g_autofree char *saved_order =
      g_settings_get_string(self->settings, "category-sort");
  GSimpleAction *sort_action = g_simple_action_new_stateful(
      "sort-order", G_VARIANT_TYPE_STRING, g_variant_new_string(saved_order));
  g_signal_connect(sort_action, "change-state",
                   G_CALLBACK(on_sort_order_changed), self);
  g_action_map_add_action(G_ACTION_MAP(self), G_ACTION(sort_action));
  GSimpleAction *restore_action =
      g_simple_action_new("restore-default-order", NULL);
  g_signal_connect(restore_action, "activate",
                   G_CALLBACK(on_restore_default_order), self);
  g_action_map_add_action(G_ACTION_MAP(self), G_ACTION(restore_action));
  g_object_unref(restore_action);
  current_sort_order =
      g_variant_get_string(g_action_get_state(G_ACTION(sort_action)), NULL);
  update_sort_button_icon(self);
  g_object_unref(sort_action);

  self->back_button = gtk_button_new_from_icon_name("go-previous-symbolic");
  gtk_widget_set_visible(self->back_button, FALSE);
  adw_header_bar_pack_start(ADW_HEADER_BAR(self->header_bar),
                            self->back_button);

  self->menu_button = GTK_WIDGET(gtk_menu_button_new());
  gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(self->menu_button),
                                "open-menu-symbolic");
  GMenu *primary_menu = g_menu_new();
  g_menu_append(primary_menu, "Export…", "win.export");
  g_menu_append(primary_menu, "Import…", "win.import");
  g_menu_append(primary_menu, "Reset Kaomojis…", "win.reset-kaomojis");
  gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(self->menu_button),
                                 G_MENU_MODEL(primary_menu));
  g_object_unref(primary_menu);
  adw_header_bar_pack_end(ADW_HEADER_BAR(self->header_bar), self->menu_button);
  gtk_box_prepend(self->right_pane, self->header_bar);
  self->category_widgets =
      g_ptr_array_new_with_free_func(category_widgets_free);
  self->active_chip_index = -1;

  static const struct {
    const char *name;
    GCallback cb;
  } actions[] = {
      {"export", G_CALLBACK(on_export_activated)},
      {"import", G_CALLBACK(on_import_activated)},
      {"reset-kaomojis", G_CALLBACK(on_reset_kaomojis)},
      {"ctx-rename-category", G_CALLBACK(on_ctx_rename_category)},
      {"ctx-delete-category", G_CALLBACK(on_ctx_delete_category)},
      {"ctx-delete-emote", G_CALLBACK(on_ctx_delete_emote)},
      {"ctx-pin-emote", G_CALLBACK(on_ctx_pin_emote)},
      {"ctx-unpin-emote", G_CALLBACK(on_ctx_unpin_emote)},
  };
  for (gsize i = 0; i < G_N_ELEMENTS(actions); i++) {
    GSimpleAction *a = g_simple_action_new(actions[i].name, NULL);
    g_signal_connect(a, "activate", actions[i].cb, self);
    g_action_map_add_action(G_ACTION_MAP(self), G_ACTION(a));
    g_object_unref(a);
  }

  g_signal_connect(self->add_button, "clicked", G_CALLBACK(on_add_clicked),
                   self);
  g_signal_connect(self->back_button, "clicked", G_CALLBACK(on_back_clicked),
                   self);
  GtkWidget *choice_page = build_add_choice_page(self);
  gtk_stack_add_named(self->view_stack, choice_page, "add-choice");
  GtkWidget *cat_page = build_add_category_page(self);
  gtk_stack_add_named(self->view_stack, cat_page, "add-category");
  GtkWidget *pick_page = build_pick_category_page_widget();
  gtk_stack_add_named(self->view_stack, pick_page, "pick-category");
  GtkWidget *entry_page = build_add_entry_page(self);
  gtk_stack_add_named(self->view_stack, entry_page, "add-entry");
  reload_categories(self);
  g_signal_connect(self->sidebar_toggle, "toggled",
                   G_CALLBACK(on_sidebar_toggled), self);
  g_signal_connect(self, "realize", G_CALLBACK(on_window_realize), NULL);
  gtk_search_entry_set_key_capture_widget(self->search_entry, GTK_WIDGET(self));
  g_signal_connect(self->search_entry, "search-changed",
                   G_CALLBACK(on_search_changed), self);
  g_signal_connect(self->kaomoji_scroll, "map",
                   G_CALLBACK(on_kaomoji_scroll_map), self);
}

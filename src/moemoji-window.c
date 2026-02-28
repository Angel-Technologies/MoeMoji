#include "moemoji-window.h"
#include "moemoji-config.h"
#include "moemoji-internal.h"

#include <string.h>

G_DEFINE_TYPE(MoeMojiWindow, moemoji_window, GTK_TYPE_APPLICATION_WINDOW)

static void category_widgets_free(gpointer data) {
  CategoryWidgets *cw = data;
  g_free(cw->name);
  g_free(cw);
}

char *make_display_name(const char *dirname) {
  char *name = g_strdup(dirname);
  for (char *p = name; *p; p++) {
    if (*p == '_')
      *p = ' ';
  }
  if (name[0])
    name[0] = g_ascii_toupper(name[0]);
  return name;
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

static void on_kaomoji_clicked(GtkButton *button,
                               G_GNUC_UNUSED gpointer user_data) {
  const char *full = g_object_get_data(G_OBJECT(button), "full-text");
  const char *text = full ? full : gtk_button_get_label(button);
  GdkClipboard *clipboard = gtk_widget_get_clipboard(GTK_WIDGET(button));
  gdk_clipboard_set_text(clipboard, text);
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

static void add_kaomoji_button(GtkFlowBox *flow, const char *text) {
  gboolean multiline = (strchr(text, '\n') != NULL);
  const char *label_text = text;
  g_autofree char *first_line = NULL;
  if (multiline) {
    const char *nl = strchr(text, '\n');
    first_line = g_strndup(text, nl - text);
    label_text = first_line;
  }
  GtkWidget *button = gtk_button_new_with_label(label_text);
  gtk_widget_add_css_class(button, "kaomoji-button");
  gtk_widget_add_css_class(button, "flat");
  if (multiline || g_utf8_strlen(label_text, -1) > 20)
    gtk_widget_add_css_class(button, "kaomoji-wide");
  if (multiline)
    g_object_set_data_full(G_OBJECT(button), "full-text", g_strdup(text),
                           g_free);

  g_signal_connect(button, "clicked", G_CALLBACK(on_kaomoji_clicked), NULL);
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

static void load_category(MoeMojiWindow *self, const char *kaomoji_dir,
                          const char *dirname) {
  char *cat_path = g_build_filename(kaomoji_dir, dirname, NULL);
  GDir *dir = g_dir_open(cat_path, 0, NULL);
  if (!dir) {
    g_free(cat_path);
    return;
  }
  char *display_name = make_display_name(dirname);
  GtkWidget *header = gtk_label_new(display_name);
  gtk_widget_add_css_class(header, "category-header");
  gtk_label_set_xalign(GTK_LABEL(header), 0.0);
  gtk_box_append(self->content_box, header);
  GtkWidget *flow = gtk_flow_box_new();
  gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(flow), FALSE);
  gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(flow), 2);
  gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flow), 10);
  gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow), GTK_SELECTION_NONE);
  const char *filename;
  while ((filename = g_dir_read_name(dir)) != NULL) {
    if (!g_str_has_suffix(filename, ".txt"))
      continue;
    char *filepath = g_build_filename(cat_path, filename, NULL);
    char *contents = NULL;
    if (g_file_get_contents(filepath, &contents, NULL, NULL)) {
      g_strchomp(contents);
      if (contents[0] != '\0')
        add_kaomoji_button(GTK_FLOW_BOX(flow), contents);
      g_free(contents);
    }
    g_free(filepath);
  }
  gtk_box_append(self->content_box, flow);
  CategoryWidgets *cw = g_new0(CategoryWidgets, 1);
  cw->header = header;
  cw->flow = flow;
  cw->name = display_name;
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

static void scroll_to_category(MoeMojiWindow *self, int index) {
  CategoryWidgets *cw = g_ptr_array_index(self->category_widgets, index);
  graphene_point_t p;
  if (gtk_widget_compute_point(cw->header, GTK_WIDGET(self->content_box),
                               &GRAPHENE_POINT_INIT(0, 0), &p)) {
    GtkAdjustment *vadj = gtk_scrolled_window_get_vadjustment(
        GTK_SCROLLED_WINDOW(self->kaomoji_scroll));
    gtk_adjustment_set_value(vadj, p.y);
  }
}

static void on_chip_clicked(GtkButton *button, gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  for (guint i = 0; i < self->category_widgets->len; i++) {
    CategoryWidgets *cw = g_ptr_array_index(self->category_widgets, i);
    if (cw->chip == GTK_WIDGET(button)) {
      scroll_to_category(self, (int)i);
      return;
    }
  }
}

static void update_chip_from_scroll(MoeMojiWindow *self) {
  double scroll_pos = gtk_adjustment_get_value(
      gtk_scrolled_window_get_vadjustment(
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

static void on_scroll_changed(GtkAdjustment *adj, gpointer user_data) {
  (void)adj;
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
  if (query == NULL || query[0] == '\0')
    return;
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

static void moemoji_window_class_init(MoeMojiWindowClass *klass) {
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
  gtk_widget_class_set_template_from_resource(
      widget_class, "/jp/angeltech/MoeMoji/moemoji-window.ui");
  gtk_widget_class_bind_template_child(widget_class, MoeMojiWindow, outer_box);
  gtk_widget_class_bind_template_child(widget_class, MoeMojiWindow,
                                       content_box);
  gtk_widget_class_bind_template_child(widget_class, MoeMojiWindow,
                                       search_entry);
  gtk_widget_class_bind_template_child(widget_class, MoeMojiWindow, header_bar);
  gtk_widget_class_bind_template_child(widget_class, MoeMojiWindow,
                                       kaomoji_scroll);
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

static gint compare_entries(gconstpointer a, gconstpointer b) {
  char **ea = *(char ***)a;
  char **eb = *(char ***)b;
  g_autofree char *ka = g_utf8_collate_key_for_filename(ea[1], -1);
  g_autofree char *kb = g_utf8_collate_key_for_filename(eb[1], -1);
  return strcmp(ka, kb);
}

static void moemoji_window_init(MoeMojiWindow *self) {
  gtk_widget_init_template(GTK_WIDGET(self));
  gtk_widget_add_css_class(GTK_WIDGET(self), "wallpaper-bg");
  gtk_widget_add_css_class(GTK_WIDGET(self->header_bar), "wallpaper-bg");
  gtk_widget_add_css_class(GTK_WIDGET(self->content_box), "content-area");
  self->category_widgets =
      g_ptr_array_new_with_free_func(category_widgets_free);
  self->active_chip_index = -1;
  char *kaomoji_dir = find_kaomoji_dir();
  g_autofree char *user_dir =
      g_build_filename(g_get_user_data_dir(), "moemoji", "kaomoji", NULL);
  GHashTable *seen =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  GPtrArray *entries = g_ptr_array_new();
  if (g_file_test(user_dir, G_FILE_TEST_IS_DIR))
    collect_categories(user_dir, seen, entries);
  if (kaomoji_dir)
    collect_categories(kaomoji_dir, seen, entries);
  g_hash_table_destroy(seen);
  if (entries->len == 0) {
    GtkWidget *label = gtk_label_new("No kaomoji data found.");
    gtk_box_append(self->content_box, label);
    g_ptr_array_free(entries, TRUE);
    g_free(kaomoji_dir);
    return;
  }
  g_ptr_array_sort(entries, compare_entries);
  for (guint i = 0; i < entries->len; i++) {
    char **pair = g_ptr_array_index(entries, i);
    load_category(self, pair[0], pair[1]);
    g_free(pair[0]);
    g_free(pair[1]);
    g_free(pair);
  }
  g_ptr_array_free(entries, TRUE);
  g_free(kaomoji_dir);
  self->bottom_spacer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_append(self->content_box, self->bottom_spacer);
  g_signal_connect(self->search_entry, "search-changed",
                   G_CALLBACK(on_search_changed), self);
  if (self->category_widgets->len > 0) {
    GtkWidget *flow = gtk_flow_box_new();
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(flow), FALSE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flow), 20);
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(flow, "category-chips");
    for (guint i = 0; i < self->category_widgets->len; i++) {
      CategoryWidgets *cw = g_ptr_array_index(self->category_widgets, i);
      GtkWidget *btn = gtk_button_new_with_label(cw->name);
      gtk_widget_add_css_class(btn, "category-chip");
      gtk_widget_add_css_class(btn, "flat");
      g_signal_connect(btn, "clicked", G_CALLBACK(on_chip_clicked), self);
      cw->chip = btn;
      gtk_flow_box_insert(GTK_FLOW_BOX(flow), btn, -1);
    }
    self->category_bar = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
    gtk_widget_add_css_class(GTK_WIDGET(self->category_bar), "category-bar");
    gtk_widget_set_size_request(GTK_WIDGET(self->category_bar), -1, 80);
    GtkWidget *cat_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(cat_scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(cat_scroll),
                                               80);
    gtk_scrolled_window_set_propagate_natural_height(
        GTK_SCROLLED_WINDOW(cat_scroll), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(cat_scroll), flow);
    gtk_box_append(self->category_bar, cat_scroll);
    gtk_box_append(self->outer_box, GTK_WIDGET(self->category_bar));

    GtkAdjustment *vadj = gtk_scrolled_window_get_vadjustment(
        GTK_SCROLLED_WINDOW(self->kaomoji_scroll));
    g_signal_connect(vadj, "value-changed", G_CALLBACK(on_scroll_changed),
                     self);
    g_signal_connect(vadj, "notify::page-size",
                     G_CALLBACK(on_page_size_changed), self);
    g_signal_connect(self->kaomoji_scroll, "map",
                     G_CALLBACK(on_kaomoji_scroll_map), self);
  }
}

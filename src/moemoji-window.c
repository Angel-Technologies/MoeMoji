#include "moemoji-window.h"
#include "moemoji-config.h"
#include "moemoji-internal.h"

#include <adwaita.h>
#include <glib/gstdio.h>
#include <string.h>

G_DEFINE_TYPE(MoeMojiWindow, moemoji_window, GTK_TYPE_APPLICATION_WINDOW)

static void reload_categories(MoeMojiWindow *self);

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
  GtkWidget *label = gtk_button_get_child(GTK_BUTTON(button));
  gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
  gtk_label_set_max_width_chars(GTK_LABEL(label), 20);
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
        add_kaomoji_button(GTK_FLOW_BOX(flow), contents);
      g_free(contents);
    }
  }
  g_ptr_array_free(files, TRUE);
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

static GPtrArray *collect_all_categories(void) {
  g_autofree char *user_dir =
      g_build_filename(g_get_user_data_dir(), "moemoji", "kaomoji", NULL);
  char *kaomoji_dir = find_kaomoji_dir();
  GHashTable *seen =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  GPtrArray *entries = g_ptr_array_new();
  if (g_file_test(user_dir, G_FILE_TEST_IS_DIR))
    collect_categories(user_dir, seen, entries);
  if (kaomoji_dir)
    collect_categories(kaomoji_dir, seen, entries);
  g_hash_table_destroy(seen);
  g_ptr_array_sort(entries, compare_entries);
  g_free(kaomoji_dir);
  return entries;
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
  gtk_widget_set_visible(self->add_button, is_main);
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
  gboolean has_non_space = FALSE;
  for (const char *p = text; *p;) {
    gunichar ch = g_utf8_get_char(p);
    if (g_unichar_isalpha(ch) || g_unichar_isdigit(ch) || ch == '_' ||
        ch == '-') {
      has_non_space = TRUE;
    } else if (ch == ' ') {
      /* space is allowed */
    } else {
      return FALSE;
    }
    p = g_utf8_next_char(p);
  }
  return has_non_space;
}

static char *normalize_category_name(const char *name) {
  char *n = g_ascii_strdown(name, -1);
  for (char *p = n; *p; p++) {
    if (*p == ' ')
      *p = '_';
  }
  return n;
}

static gboolean dir_has_category(const char *base, const char *norm_name) {
  GDir *dir = g_dir_open(base, 0, NULL);
  if (!dir)
    return FALSE;
  const char *name;
  while ((name = g_dir_read_name(dir)) != NULL) {
    g_autofree char *norm = normalize_category_name(name);
    if (strcmp(norm, norm_name) == 0) {
      g_autofree char *full = g_build_filename(base, name, NULL);
      if (g_file_test(full, G_FILE_TEST_IS_DIR)) {
        g_dir_close(dir);
        return TRUE;
      }
    }
  }
  g_dir_close(dir);
  return FALSE;
}

static gboolean category_exists(const char *text) {
  g_autofree char *stripped = g_strstrip(g_strdup(text));
  g_autofree char *norm = normalize_category_name(stripped);
  g_autofree char *user_base =
      g_build_filename(g_get_user_data_dir(), "moemoji", "kaomoji", NULL);
  if (dir_has_category(user_base, norm))
    return TRUE;
  char *bundled = find_kaomoji_dir();
  if (bundled) {
    gboolean exists = dir_has_category(bundled, norm);
    g_free(bundled);
    if (exists)
      return TRUE;
  }
  return FALSE;
}

static void on_category_name_changed(G_GNUC_UNUSED GtkEditable *editable,
                                     gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  const char *text =
      gtk_editable_get_text(GTK_EDITABLE(self->category_name_entry));
  gtk_widget_set_sensitive(self->category_save_button,
                           is_valid_category_name(text) &&
                               !category_exists(text));
}

static void on_category_save_clicked(G_GNUC_UNUSED GtkButton *button,
                                     gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  const char *text =
      gtk_editable_get_text(GTK_EDITABLE(self->category_name_entry));
  if (!is_valid_category_name(text) || category_exists(text))
    return;
  g_autofree char *dir_name = g_strdup(text);
  g_strstrip(dir_name);
  for (char *p = dir_name; *p; p++) {
    if (*p == ' ')
      *p = '_';
  }
  g_autofree char *cat_path = g_build_filename(g_get_user_data_dir(), "moemoji",
                                               "kaomoji", dir_name, NULL);
  g_mkdir_with_parents(cat_path, 0755);

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
    g_autofree char *display = make_display_name(pair[1]);
    g_autofree char *full_path = g_build_filename(pair[0], pair[1], NULL);
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

static void reload_categories(MoeMojiWindow *self) {
  GtkWidget *child;
  while ((child = gtk_widget_get_first_child(GTK_WIDGET(self->content_box))) !=
         NULL)
    gtk_box_remove(self->content_box, child);
  g_ptr_array_set_size(self->category_widgets, 0);
  self->active_chip_index = -1;
  self->bottom_spacer = NULL;

  if (self->category_bar) {
    gtk_box_remove(self->outer_box, GTK_WIDGET(self->category_bar));
    self->category_bar = NULL;
  }
  GPtrArray *entries = collect_all_categories();

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

  self->category_save_button = gtk_button_new_with_label("Save");
  gtk_widget_set_sensitive(self->category_save_button, FALSE);
  gtk_widget_add_css_class(self->category_save_button, "suggested-action");
  g_signal_connect(self->category_save_button, "clicked",
                   G_CALLBACK(on_category_save_clicked), self);
  gtk_box_append(GTK_BOX(box), self->category_save_button);

  return box;
}

static GtkWidget *build_pick_category_page_widget(MoeMojiWindow *self) {
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

  (void)self;
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

static void populate_manage_page(MoeMojiWindow *self);

static void on_manage_delete_emote_response(AdwAlertDialog *dialog,
                                            GAsyncResult *res,
                                            gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  const char *response = adw_alert_dialog_choose_finish(dialog, res);
  if (g_strcmp0(response, "yes") == 0) {
    const char *filepath = g_object_get_data(G_OBJECT(dialog), "emote-path");
    g_unlink(filepath);
    reload_categories(self);
    populate_manage_page(self);
  }
}

static void on_manage_delete_emote(GtkButton *button, gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  const char *filepath = g_object_get_data(G_OBJECT(button), "emote-path");
  const char *emote_text = g_object_get_data(G_OBJECT(button), "emote-text");

  g_autofree char *body = NULL;
  if (g_utf8_strlen(emote_text, -1) > 40) {
    gchar *end = g_utf8_offset_to_pointer(emote_text, 40);
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
    populate_manage_page(self);
  }
}

static void on_manage_delete_category(GtkButton *button, gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  const char *cat_path = g_object_get_data(G_OBJECT(button), "cat-path");
  const char *cat_name = g_object_get_data(G_OBJECT(button), "cat-name");

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

static void populate_manage_page(MoeMojiWindow *self) {
  GtkWidget *manage_page =
      gtk_stack_get_child_by_name(self->view_stack, "manage");
  GtkWidget *content =
      g_object_get_data(G_OBJECT(manage_page), "manage-content");

  GtkWidget *child;
  while ((child = gtk_widget_get_first_child(content)) != NULL)
    gtk_box_remove(GTK_BOX(content), child);

  GPtrArray *entries = collect_all_categories();

  if (entries->len == 0) {
    GtkWidget *label = gtk_label_new("No categories to manage.");
    gtk_box_append(GTK_BOX(content), label);
  }

  for (guint i = 0; i < entries->len; i++) {
    char **pair = g_ptr_array_index(entries, i);
    g_autofree char *cat_path = g_build_filename(pair[0], pair[1], NULL);
    g_autofree char *display_name = make_display_name(pair[1]);

    GtkWidget *cat_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(cat_hbox, 8);

    GtkWidget *cat_label = gtk_label_new(display_name);
    gtk_widget_add_css_class(cat_label, "category-header");
    gtk_label_set_xalign(GTK_LABEL(cat_label), 0.0);
    gtk_widget_set_hexpand(cat_label, TRUE);
    gtk_box_append(GTK_BOX(cat_hbox), cat_label);

    GtkWidget *cat_del_btn =
        gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_widget_add_css_class(cat_del_btn, "manage-cat-delete");
    gtk_widget_add_css_class(cat_del_btn, "circular");
    gtk_widget_set_valign(cat_del_btn, GTK_ALIGN_CENTER);
    g_object_set_data_full(G_OBJECT(cat_del_btn), "cat-path",
                           g_strdup(cat_path), g_free);
    g_object_set_data_full(G_OBJECT(cat_del_btn), "cat-name",
                           g_strdup(display_name), g_free);
    g_signal_connect(cat_del_btn, "clicked",
                     G_CALLBACK(on_manage_delete_category), self);
    gtk_box_append(GTK_BOX(cat_hbox), cat_del_btn);

    gtk_box_append(GTK_BOX(content), cat_hbox);

    GtkWidget *flow = gtk_flow_box_new();
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(flow), FALSE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(flow), 2);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flow), 10);
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow), GTK_SELECTION_NONE);

    GDir *dir = g_dir_open(cat_path, 0, NULL);
    if (dir) {
      GPtrArray *files = g_ptr_array_new_with_free_func(g_free);
      const char *filename;
      while ((filename = g_dir_read_name(dir)) != NULL) {
        if (g_str_has_suffix(filename, ".txt"))
          g_ptr_array_add(files, g_build_filename(cat_path, filename, NULL));
      }
      g_ptr_array_sort(files, compare_files_by_mtime);

      for (guint j = 0; j < files->len; j++) {
        const char *filepath = g_ptr_array_index(files, j);
        char *contents = NULL;
        if (g_file_get_contents(filepath, &contents, NULL, NULL)) {
          g_strchomp(contents);
          if (contents[0] != '\0') {
            GtkWidget *emote_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
            gtk_widget_add_css_class(emote_box, "manage-emote");
            gtk_widget_set_halign(emote_box, GTK_ALIGN_CENTER);

            const char *display_text = contents;
            g_autofree char *first_line = NULL;
            if (strchr(contents, '\n')) {
              const char *nl = strchr(contents, '\n');
              first_line = g_strndup(contents, nl - contents);
              display_text = first_line;
            }

            GtkWidget *emote_label = gtk_label_new(display_text);
            gtk_label_set_ellipsize(GTK_LABEL(emote_label),
                                    PANGO_ELLIPSIZE_END);
            gtk_label_set_max_width_chars(GTK_LABEL(emote_label), 20);
            gtk_box_append(GTK_BOX(emote_box), emote_label);

            GtkWidget *del_btn =
                gtk_button_new_from_icon_name("window-close-symbolic");
            gtk_widget_add_css_class(del_btn, "manage-delete");
            gtk_widget_add_css_class(del_btn, "circular");
            gtk_widget_set_halign(del_btn, GTK_ALIGN_CENTER);
            g_object_set_data_full(G_OBJECT(del_btn), "emote-path",
                                   g_strdup(filepath), g_free);
            g_object_set_data_full(G_OBJECT(del_btn), "emote-text",
                                   g_strdup(display_text), g_free);
            g_signal_connect(del_btn, "clicked",
                             G_CALLBACK(on_manage_delete_emote), self);
            gtk_box_append(GTK_BOX(emote_box), del_btn);

            gtk_flow_box_insert(GTK_FLOW_BOX(flow), emote_box, -1);
          }
          g_free(contents);
        }
      }
      g_ptr_array_free(files, TRUE);
      g_dir_close(dir);
    }

    gtk_box_append(GTK_BOX(content), flow);
  }
  free_category_entries(entries);
}

static void on_manage_search_changed(GtkSearchEntry *entry,
                                     gpointer user_data) {
  GtkWidget *manage_page = GTK_WIDGET(user_data);
  const char *query = gtk_editable_get_text(GTK_EDITABLE(entry));
  if (query == NULL || query[0] == '\0')
    return;

  GtkWidget *content =
      g_object_get_data(G_OBJECT(manage_page), "manage-content");
  GtkWidget *scroll = g_object_get_data(G_OBJECT(manage_page), "manage-scroll");

  g_autofree char *query_lower = g_utf8_strdown(query, -1);

  for (GtkWidget *child = gtk_widget_get_first_child(content); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    GtkWidget *first = gtk_widget_get_first_child(child);
    if (first == NULL || !GTK_IS_LABEL(first))
      continue;
    const char *label_text = gtk_label_get_text(GTK_LABEL(first));
    g_autofree char *name_lower = g_utf8_strdown(label_text, -1);
    if (strstr(name_lower, query_lower) != NULL) {
      graphene_point_t p;
      if (gtk_widget_compute_point(child, content, &GRAPHENE_POINT_INIT(0, 0),
                                   &p)) {
        GtkAdjustment *vadj =
            gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroll));
        gtk_adjustment_set_value(vadj, p.y);
      }
      return;
    }
  }
}

static GtkWidget *build_manage_page(void) {
  GtkWidget *wrapper = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_vexpand(wrapper, TRUE);

  GtkWidget *clamp = adw_clamp_new();
  adw_clamp_set_maximum_size(ADW_CLAMP(clamp), 600);
  adw_clamp_set_tightening_threshold(ADW_CLAMP(clamp), 400);
  gtk_widget_set_vexpand(clamp, TRUE);

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_add_css_class(box, "add-form");
  gtk_widget_set_vexpand(box, TRUE);

  GtkWidget *search = gtk_search_entry_new();
  gtk_widget_set_margin_start(search, 6);
  gtk_widget_set_margin_end(search, 6);
  gtk_widget_set_margin_top(search, 6);
  g_object_set(search, "placeholder-text", "Jump to category...", NULL);
  gtk_box_append(GTK_BOX(box), search);

  GtkWidget *scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER,
                                 GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand(scroll, TRUE);

  GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  gtk_widget_set_margin_start(content, 6);
  gtk_widget_set_margin_end(content, 6);
  gtk_widget_set_margin_top(content, 6);
  gtk_widget_set_margin_bottom(content, 6);

  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), content);
  gtk_box_append(GTK_BOX(box), scroll);
  adw_clamp_set_child(ADW_CLAMP(clamp), box);
  gtk_box_append(GTK_BOX(wrapper), clamp);

  g_object_set_data(G_OBJECT(wrapper), "manage-content", content);
  g_object_set_data(G_OBJECT(wrapper), "manage-search", search);
  g_object_set_data(G_OBJECT(wrapper), "manage-scroll", scroll);

  g_signal_connect(search, "search-changed",
                   G_CALLBACK(on_manage_search_changed), wrapper);

  return wrapper;
}

static void on_manage_activated(G_GNUC_UNUSED GSimpleAction *action,
                                G_GNUC_UNUSED GVariant *parameter,
                                gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  populate_manage_page(self);
  GtkWidget *manage_page =
      gtk_stack_get_child_by_name(self->view_stack, "manage");
  GtkWidget *search = g_object_get_data(G_OBJECT(manage_page), "manage-search");
  gtk_editable_set_text(GTK_EDITABLE(search), "");
  navigate_to(self, "manage", TRUE);
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
  g_autofree char *user_dir =
      g_build_filename(g_get_user_data_dir(), "moemoji", "kaomoji", NULL);
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
  char *kaomoji_dir = find_kaomoji_dir();
  if (kaomoji_dir) {
    GDir *dir = g_dir_open(kaomoji_dir, 0, NULL);
    if (dir) {
      const char *name;
      while ((name = g_dir_read_name(dir)) != NULL) {
        g_autofree char *dst = g_build_filename(tmpdir, name, NULL);
        if (g_file_test(dst, G_FILE_TEST_IS_DIR))
          continue;
        g_autofree char *src = g_build_filename(kaomoji_dir, name, NULL);
        if (g_file_test(src, G_FILE_TEST_IS_DIR))
          copy_dir_recursive(src, dst);
      }
      g_dir_close(dir);
    }
    g_free(kaomoji_dir);
  }
  GSubprocess *tar = g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, NULL, "tar",
                                      "czf", path, "-C", tmpdir, ".", NULL);
  if (tar) {
    g_subprocess_wait(tar, NULL, NULL);
    g_object_unref(tar);
  }

  remove_dir_recursive(tmpdir);
}

static void on_export_activated(G_GNUC_UNUSED GSimpleAction *action,
                                G_GNUC_UNUSED GVariant *parameter,
                                gpointer user_data) {
  MoeMojiWindow *self = MOEMOJI_WINDOW(user_data);
  GtkFileDialog *dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, "Export Kaomojis");
  gtk_file_dialog_set_initial_name(dialog, "moemoji-export.tar.gz");
  GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
  GtkFileFilter *filter = gtk_file_filter_new();
  gtk_file_filter_set_name(filter, "Tar archives (*.tar.gz)");
  gtk_file_filter_add_pattern(filter, "*.tar.gz");
  g_list_store_append(filters, filter);
  g_object_unref(filter);
  gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
  g_object_unref(filters);
  gtk_file_dialog_save(dialog, GTK_WINDOW(self), NULL, on_export_save_ready,
                       self);
  g_object_unref(dialog);
}

static void clear_user_categories(void) {
  g_autofree char *user_dir =
      g_build_filename(g_get_user_data_dir(), "moemoji", "kaomoji", NULL);
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
  g_autofree char *user_dir =
      g_build_filename(g_get_user_data_dir(), "moemoji", "kaomoji", NULL);
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
  GtkFileDialog *dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, "Import Kaomojis");
  GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
  GtkFileFilter *filter = gtk_file_filter_new();
  gtk_file_filter_set_name(filter, "Tar archives (*.tar.gz)");
  gtk_file_filter_add_pattern(filter, "*.tar.gz");
  g_list_store_append(filters, filter);
  g_object_unref(filter);
  gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
  g_object_unref(filters);
  gtk_file_dialog_open(dialog, GTK_WINDOW(self), NULL, on_import_open_ready,
                       self);
  g_object_unref(dialog);
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
  gtk_widget_class_bind_template_child(widget_class, MoeMojiWindow, view_stack);
  gtk_widget_class_bind_template_child(widget_class, MoeMojiWindow, add_button);
  gtk_widget_class_bind_template_child(widget_class, MoeMojiWindow,
                                       back_button);
  gtk_widget_class_bind_template_child(widget_class, MoeMojiWindow,
                                       menu_button);
}

static void moemoji_window_init(MoeMojiWindow *self) {
  gtk_widget_init_template(GTK_WIDGET(self));
  gtk_widget_add_css_class(GTK_WIDGET(self), "wallpaper-bg");
  gtk_widget_add_css_class(GTK_WIDGET(self->header_bar), "wallpaper-bg");
  gtk_widget_add_css_class(GTK_WIDGET(self->content_box), "content-area");
  self->category_widgets =
      g_ptr_array_new_with_free_func(category_widgets_free);
  self->active_chip_index = -1;
  self->selected_category_dir = NULL;

  GSimpleAction *export_action = g_simple_action_new("export", NULL);
  g_signal_connect(export_action, "activate", G_CALLBACK(on_export_activated),
                   self);
  g_action_map_add_action(G_ACTION_MAP(self), G_ACTION(export_action));
  g_object_unref(export_action);

  GSimpleAction *import_action = g_simple_action_new("import", NULL);
  g_signal_connect(import_action, "activate", G_CALLBACK(on_import_activated),
                   self);
  g_action_map_add_action(G_ACTION_MAP(self), G_ACTION(import_action));
  g_object_unref(import_action);

  GSimpleAction *manage_action = g_simple_action_new("manage", NULL);
  g_signal_connect(manage_action, "activate", G_CALLBACK(on_manage_activated),
                   self);
  g_action_map_add_action(G_ACTION_MAP(self), G_ACTION(manage_action));
  g_object_unref(manage_action);

  g_signal_connect(self->add_button, "clicked", G_CALLBACK(on_add_clicked),
                   self);
  g_signal_connect(self->back_button, "clicked", G_CALLBACK(on_back_clicked),
                   self);
  GtkWidget *choice_page = build_add_choice_page(self);
  gtk_stack_add_named(self->view_stack, choice_page, "add-choice");
  GtkWidget *cat_page = build_add_category_page(self);
  gtk_stack_add_named(self->view_stack, cat_page, "add-category");
  GtkWidget *pick_page = build_pick_category_page_widget(self);
  gtk_stack_add_named(self->view_stack, pick_page, "pick-category");
  GtkWidget *entry_page = build_add_entry_page(self);
  gtk_stack_add_named(self->view_stack, entry_page, "add-entry");
  GtkWidget *manage_page = build_manage_page();
  gtk_stack_add_named(self->view_stack, manage_page, "manage");
  reload_categories(self);
  g_signal_connect(self->search_entry, "search-changed",
                   G_CALLBACK(on_search_changed), self);
  g_signal_connect(self->kaomoji_scroll, "map",
                   G_CALLBACK(on_kaomoji_scroll_map), self);
}

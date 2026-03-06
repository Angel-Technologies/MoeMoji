#include "moemoji-internal.h"
#include "moemoji-window.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

static void test_display_name_underscores(void) {
  char *name = make_display_name("happy_faces");
  g_assert_cmpstr(name, ==, "happy faces");
  g_free(name);
}

static void test_display_name_no_underscores(void) {
  char *name = make_display_name("animals");
  g_assert_cmpstr(name, ==, "animals");
  g_free(name);
}

static void test_display_name_already_upper(void) {
  char *name = make_display_name("Animals");
  g_assert_cmpstr(name, ==, "Animals");
  g_free(name);
}

static void test_display_name_single_char(void) {
  char *name = make_display_name("x");
  g_assert_cmpstr(name, ==, "x");
  g_free(name);
}

static void test_display_name_empty(void) {
  char *name = make_display_name("");
  g_assert_cmpstr(name, ==, "");
  g_free(name);
}

static void test_find_kaomoji_with_env(void) {
  char *dir = find_kaomoji_dir();
  g_assert_nonnull(dir);
  g_assert_true(g_file_test(dir, G_FILE_TEST_IS_DIR));
  g_free(dir);
}

static gboolean find_kaomoji_bogus_passed = FALSE;

static void run_find_kaomoji_bogus(void) {
  g_setenv("MESON_SOURCE_ROOT", "/nonexistent", TRUE);
  char *saved = g_get_current_dir();
  g_assert_true(g_chdir("/tmp") == 0);

  char *dir = find_kaomoji_dir();
  g_free(dir);

  g_assert_true(g_chdir(saved) == 0);
  g_free(saved);
  g_setenv("MESON_SOURCE_ROOT", SRCDIR, TRUE);
  find_kaomoji_bogus_passed = TRUE;
}

static void test_find_kaomoji_bogus(void) {
  g_assert_true(find_kaomoji_bogus_passed);
}

static void test_sni_category(void) {
  GVariant *v =
      sni_get_property(NULL, NULL, NULL, NULL, "Category", NULL, NULL);
  g_assert_nonnull(v);
  g_assert_cmpstr(g_variant_get_string(v, NULL), ==, "ApplicationStatus");
  g_variant_unref(v);
}

static void test_sni_id(void) {
  GVariant *v = sni_get_property(NULL, NULL, NULL, NULL, "Id", NULL, NULL);
  g_assert_nonnull(v);
  g_assert_cmpstr(g_variant_get_string(v, NULL), ==, "moemoji");
  g_variant_unref(v);
}

static void test_sni_item_is_menu(void) {
  GVariant *v =
      sni_get_property(NULL, NULL, NULL, NULL, "ItemIsMenu", NULL, NULL);
  g_assert_nonnull(v);
  g_assert_false(g_variant_get_boolean(v));
  g_variant_unref(v);
}

static void test_sni_menu(void) {
  GVariant *v = sni_get_property(NULL, NULL, NULL, NULL, "Menu", NULL, NULL);
  g_assert_nonnull(v);
  g_assert_cmpstr(g_variant_get_string(v, NULL), ==, "/MenuBar");
  g_variant_unref(v);
}

static void test_sni_unknown(void) {
  GVariant *v =
      sni_get_property(NULL, NULL, NULL, NULL, "Nonexistent", NULL, NULL);
  g_assert_null(v);
}

static void test_dbusmenu_version(void) {
  GVariant *v =
      dbusmenu_get_property(NULL, NULL, NULL, NULL, "Version", NULL, NULL);
  g_assert_nonnull(v);
  g_assert_cmpuint(g_variant_get_uint32(v), ==, 3);
  g_variant_unref(v);
}

static void test_dbusmenu_status(void) {
  GVariant *v =
      dbusmenu_get_property(NULL, NULL, NULL, NULL, "Status", NULL, NULL);
  g_assert_nonnull(v);
  g_assert_cmpstr(g_variant_get_string(v, NULL), ==, "normal");
  g_variant_unref(v);
}

static void test_dbusmenu_text_direction(void) {
  GVariant *v = dbusmenu_get_property(NULL, NULL, NULL, NULL, "TextDirection",
                                      NULL, NULL);
  g_assert_nonnull(v);
  g_assert_cmpstr(g_variant_get_string(v, NULL), ==, "ltr");
  g_variant_unref(v);
}

static void test_dbusmenu_unknown(void) {
  GVariant *v =
      dbusmenu_get_property(NULL, NULL, NULL, NULL, "Bogus", NULL, NULL);
  g_assert_null(v);
}

static void test_category_widgets_chip_init(void) {
  CategoryWidgets *cw = g_new0(CategoryWidgets, 1);
  g_assert_null(cw->chip);
  g_assert_null(cw->header);
  g_assert_null(cw->flow);
  g_assert_null(cw->name);
  g_free(cw);
}

G_GNUC_INTERNAL GResource *moemoji_get_resource(void);

static GResource *test_res = NULL;
static GtkWidget *test_win = NULL;
static MoeMojiWindow *test_self = NULL;

static void cleanup_user_kaomoji_dir(void) {
  char *kaomoji_parent =
      g_build_filename(g_get_user_data_dir(), "moemoji", "kaomoji", NULL);
  g_rmdir(kaomoji_parent);
  g_free(kaomoji_parent);
  char *moemoji_parent =
      g_build_filename(g_get_user_data_dir(), "moemoji", NULL);
  g_rmdir(moemoji_parent);
  g_free(moemoji_parent);
}

static void window_test_setup(void) {
  if (!test_res) {
    test_res = moemoji_get_resource();
    g_resources_register(test_res);
  }
  test_win = g_object_new(MOEMOJI_TYPE_WINDOW, NULL);
  test_self = MOEMOJI_WINDOW(test_win);
}

static void window_test_teardown(void) {
  gtk_window_destroy(GTK_WINDOW(test_win));
  test_win = NULL;
  test_self = NULL;
}

static void test_window_chip_setup(void) {
  window_test_setup();

  g_assert_cmpuint(test_self->category_widgets->len, >, 0);

  for (guint i = 0; i < test_self->category_widgets->len; i++) {
    CategoryWidgets *cw = g_ptr_array_index(test_self->category_widgets, i);
    g_assert_nonnull(cw->chip);
  }

  window_test_teardown();
}

static void test_window_chip_labels_match(void) {
  window_test_setup();

  for (guint i = 0; i < test_self->category_widgets->len; i++) {
    CategoryWidgets *cw = g_ptr_array_index(test_self->category_widgets, i);
    const char *chip_label = gtk_button_get_label(GTK_BUTTON(cw->chip));
    g_assert_cmpstr(chip_label, ==, cw->name);
  }

  window_test_teardown();
}

static void test_window_initial_active_chip(void) {
  window_test_setup();

  g_assert_cmpint(test_self->active_chip_index, ==, -1);

  window_test_teardown();
}

static void test_window_all_categories_visible(void) {
  window_test_setup();

  for (guint i = 0; i < test_self->category_widgets->len; i++) {
    CategoryWidgets *cw = g_ptr_array_index(test_self->category_widgets, i);
    g_assert_true(gtk_widget_get_visible(cw->header));
    g_assert_true(gtk_widget_get_visible(cw->flow));
  }

  window_test_teardown();
}

static void test_window_bottom_spacer(void) {
  window_test_setup();

  g_assert_nonnull(test_self->bottom_spacer);
  GtkWidget *last =
      gtk_widget_get_last_child(GTK_WIDGET(test_self->content_box));
  g_assert_true(last == test_self->bottom_spacer);

  window_test_teardown();
}

static void test_window_view_stack_exists(void) {
  window_test_setup();
  g_assert_nonnull(test_self->view_stack);
  g_assert_true(GTK_IS_STACK(test_self->view_stack));
  const char *name = gtk_stack_get_visible_child_name(test_self->view_stack);
  g_assert_cmpstr(name, ==, "main");

  window_test_teardown();
}

static void test_window_add_button_navigates(void) {
  window_test_setup();
  g_assert_nonnull(test_self->add_button);
  g_assert_nonnull(test_self->back_button);
  g_assert_true(gtk_widget_get_visible(test_self->add_button));
  g_assert_false(gtk_widget_get_visible(test_self->back_button));
  g_signal_emit_by_name(test_self->add_button, "clicked");
  const char *name = gtk_stack_get_visible_child_name(test_self->view_stack);
  g_assert_cmpstr(name, ==, "add-choice");
  g_assert_false(gtk_widget_get_visible(test_self->add_button));
  g_assert_true(gtk_widget_get_visible(test_self->back_button));
  g_signal_emit_by_name(test_self->back_button, "clicked");
  name = gtk_stack_get_visible_child_name(test_self->view_stack);
  g_assert_cmpstr(name, ==, "main");
  g_assert_true(gtk_widget_get_visible(test_self->add_button));
  g_assert_false(gtk_widget_get_visible(test_self->back_button));

  window_test_teardown();
}

static void test_window_stack_pages_exist(void) {
  window_test_setup();

  g_assert_nonnull(gtk_stack_get_child_by_name(test_self->view_stack, "main"));
  g_assert_nonnull(
      gtk_stack_get_child_by_name(test_self->view_stack, "add-choice"));
  g_assert_nonnull(
      gtk_stack_get_child_by_name(test_self->view_stack, "add-category"));
  g_assert_nonnull(
      gtk_stack_get_child_by_name(test_self->view_stack, "pick-category"));
  g_assert_nonnull(
      gtk_stack_get_child_by_name(test_self->view_stack, "add-entry"));
  g_assert_nonnull(
      gtk_stack_get_child_by_name(test_self->view_stack, "manage"));

  window_test_teardown();
}

static void test_add_category_creates_dir(void) {
  char *cat_dir = g_build_filename(g_get_user_data_dir(), "moemoji", "kaomoji",
                                   "ZZZ_test_category", NULL);
  g_mkdir_with_parents(cat_dir, 0755);
  window_test_setup();
  gboolean found = FALSE;
  for (guint i = 0; i < test_self->category_widgets->len; i++) {
    CategoryWidgets *cw = g_ptr_array_index(test_self->category_widgets, i);
    if (g_strcmp0(cw->name, "ZZZ test category") == 0) {
      found = TRUE;
      break;
    }
  }
  g_assert_true(found);
  window_test_teardown();
  g_rmdir(cat_dir);
  cleanup_user_kaomoji_dir();
  g_free(cat_dir);
}

static void test_add_entry_creates_file(void) {
  char *cat_dir = g_build_filename(g_get_user_data_dir(), "moemoji", "kaomoji",
                                   "ZZZ_test_entry_cat", NULL);
  g_mkdir_with_parents(cat_dir, 0755);
  char *fpath = g_build_filename(cat_dir, "1.txt", NULL);
  g_file_set_contents(fpath, "(╯°□°)╯︵ ┻━┻\n", -1, NULL);
  window_test_setup();
  gboolean found = FALSE;
  for (guint i = 0; i < test_self->category_widgets->len; i++) {
    CategoryWidgets *cw = g_ptr_array_index(test_self->category_widgets, i);
    if (g_strcmp0(cw->name, "ZZZ test entry cat") == 0) {
      GtkWidget *first = gtk_widget_get_first_child(cw->flow);
      g_assert_nonnull(first);
      found = TRUE;
      break;
    }
  }
  g_assert_true(found);

  g_free(test_self->selected_category_dir);
  test_self->selected_category_dir = g_strdup(cat_dir);
  GtkTextBuffer *buf =
      gtk_text_view_get_buffer(GTK_TEXT_VIEW(test_self->entry_text_view));
  gtk_text_buffer_set_text(buf, "( ˘ω˘ )", -1);

  GtkWidget *entry_page =
      gtk_stack_get_child_by_name(test_self->view_stack, "add-entry");
  GtkWidget *save_btn = gtk_widget_get_last_child(entry_page);
  g_assert_nonnull(save_btn);
  g_signal_emit_by_name(save_btn, "clicked");
  char *fpath2 = g_build_filename(cat_dir, "2.txt", NULL);
  g_assert_true(g_file_test(fpath2, G_FILE_TEST_EXISTS));
  char *contents = NULL;
  g_file_get_contents(fpath2, &contents, NULL, NULL);
  g_assert_nonnull(contents);
  g_strchomp(contents);
  g_assert_cmpstr(contents, ==, "( ˘ω˘ )");
  g_free(contents);
  const char *name = gtk_stack_get_visible_child_name(test_self->view_stack);
  g_assert_cmpstr(name, ==, "main");

  window_test_teardown();

  g_unlink(fpath);
  g_unlink(fpath2);
  g_rmdir(cat_dir);
  cleanup_user_kaomoji_dir();
  g_free(cat_dir);
  g_free(fpath);
  g_free(fpath2);
}

static void test_export_creates_archive(void) {
  char *user_dir =
      g_build_filename(g_get_user_data_dir(), "moemoji", "kaomoji", NULL);
  char *cat_dir = g_build_filename(user_dir, "ZZZ_export_test", NULL);
  g_mkdir_with_parents(cat_dir, 0755);
  char *fpath = g_build_filename(cat_dir, "1.txt", NULL);
  g_file_set_contents(fpath, "(ﾉ◕ヮ◕)ﾉ*:・ﾟ✧\n", -1, NULL);
  char *tmpdir = g_dir_make_tmp("moemoji-test-XXXXXX", NULL);
  g_assert_nonnull(tmpdir);
  char *archive_path = g_build_filename(tmpdir, "test-export.tar.gz", NULL);
  GSubprocess *tar =
      g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, NULL, "tar", "czf",
                       archive_path, "-C", user_dir, ".", NULL);
  g_assert_nonnull(tar);
  g_assert_true(g_subprocess_wait_check(tar, NULL, NULL));
  g_object_unref(tar);
  g_assert_true(g_file_test(archive_path, G_FILE_TEST_EXISTS));
  char *extract_dir = g_build_filename(tmpdir, "extracted", NULL);
  g_mkdir_with_parents(extract_dir, 0755);

  GSubprocess *untar =
      g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, NULL, "tar", "xzf",
                       archive_path, "-C", extract_dir, NULL);
  g_assert_nonnull(untar);
  g_assert_true(g_subprocess_wait_check(untar, NULL, NULL));
  g_object_unref(untar);

  char *extracted_file =
      g_build_filename(extract_dir, "ZZZ_export_test", "1.txt", NULL);
  g_assert_true(g_file_test(extracted_file, G_FILE_TEST_EXISTS));
  char *contents = NULL;
  g_file_get_contents(extracted_file, &contents, NULL, NULL);
  g_assert_nonnull(contents);
  g_strchomp(contents);
  g_assert_cmpstr(contents, ==, "(ﾉ◕ヮ◕)ﾉ*:・ﾟ✧");
  g_free(contents);

  g_unlink(extracted_file);
  g_free(extracted_file);
  char *extracted_cat = g_build_filename(extract_dir, "ZZZ_export_test", NULL);
  g_rmdir(extracted_cat);
  g_free(extracted_cat);
  g_rmdir(extract_dir);
  g_free(extract_dir);
  g_unlink(archive_path);
  g_free(archive_path);
  g_rmdir(tmpdir);
  g_free(tmpdir);

  g_unlink(fpath);
  g_free(fpath);
  g_rmdir(cat_dir);
  g_free(cat_dir);
  cleanup_user_kaomoji_dir();
  g_free(user_dir);
}

static void test_import_extracts_and_loads(void) {
  char *src_dir = g_dir_make_tmp("moemoji-import-src-XXXXXX", NULL);
  g_assert_nonnull(src_dir);
  char *cat_dir = g_build_filename(src_dir, "ZZZ_import_test", NULL);
  g_mkdir_with_parents(cat_dir, 0755);

  char *fpath = g_build_filename(cat_dir, "1.txt", NULL);
  g_file_set_contents(fpath, "ヽ(>∀<☆)ノ\n", -1, NULL);

  char *archive_path = g_build_filename(src_dir, "import-test.tar.gz", NULL);
  GSubprocess *tar =
      g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, NULL, "tar", "czf",
                       archive_path, "-C", src_dir, "ZZZ_import_test", NULL);
  g_assert_nonnull(tar);
  g_assert_true(g_subprocess_wait_check(tar, NULL, NULL));
  g_object_unref(tar);

  char *user_dir =
      g_build_filename(g_get_user_data_dir(), "moemoji", "kaomoji", NULL);
  g_mkdir_with_parents(user_dir, 0755);

  GSubprocess *untar =
      g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, NULL, "tar", "xzf",
                       archive_path, "-C", user_dir, NULL);
  g_assert_nonnull(untar);
  g_assert_true(g_subprocess_wait_check(untar, NULL, NULL));
  g_object_unref(untar);

  char *imported_file =
      g_build_filename(user_dir, "ZZZ_import_test", "1.txt", NULL);
  g_assert_true(g_file_test(imported_file, G_FILE_TEST_EXISTS));
  char *contents = NULL;
  g_file_get_contents(imported_file, &contents, NULL, NULL);
  g_assert_nonnull(contents);
  g_strchomp(contents);
  g_assert_cmpstr(contents, ==, "ヽ(>∀<☆)ノ");
  g_free(contents);

  window_test_setup();
  gboolean found = FALSE;
  for (guint i = 0; i < test_self->category_widgets->len; i++) {
    CategoryWidgets *cw = g_ptr_array_index(test_self->category_widgets, i);
    if (g_strcmp0(cw->name, "ZZZ import test") == 0) {
      found = TRUE;
      break;
    }
  }
  g_assert_true(found);
  window_test_teardown();

  g_unlink(imported_file);
  g_free(imported_file);
  char *imported_cat = g_build_filename(user_dir, "ZZZ_import_test", NULL);
  g_rmdir(imported_cat);
  g_free(imported_cat);
  cleanup_user_kaomoji_dir();
  g_free(user_dir);

  g_unlink(fpath);
  g_free(fpath);
  g_rmdir(cat_dir);
  g_free(cat_dir);
  g_unlink(archive_path);
  g_free(archive_path);

  GSubprocess *rm = g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, NULL, "rm", "-rf",
                                     src_dir, NULL);
  if (rm) {
    g_subprocess_wait(rm, NULL, NULL);
    g_object_unref(rm);
  }
  g_free(src_dir);
}

static void test_category_name_validation(void) {
  window_test_setup();
  gtk_editable_set_text(GTK_EDITABLE(test_self->category_name_entry), "");
  g_assert_false(gtk_widget_get_sensitive(test_self->category_save_button));
  gtk_editable_set_text(GTK_EDITABLE(test_self->category_name_entry), "   ");
  g_assert_false(gtk_widget_get_sensitive(test_self->category_save_button));
  gtk_editable_set_text(GTK_EDITABLE(test_self->category_name_entry),
                        "Happy Faces");
  g_assert_true(gtk_widget_get_sensitive(test_self->category_save_button));
  gtk_editable_set_text(GTK_EDITABLE(test_self->category_name_entry),
                        "my-cool_category");
  g_assert_true(gtk_widget_get_sensitive(test_self->category_save_button));
  gtk_editable_set_text(GTK_EDITABLE(test_self->category_name_entry),
                        "Кириллица");
  g_assert_true(gtk_widget_get_sensitive(test_self->category_save_button));
  gtk_editable_set_text(GTK_EDITABLE(test_self->category_name_entry),
                        "日本語カテゴリ");
  g_assert_true(gtk_widget_get_sensitive(test_self->category_save_button));
  gtk_editable_set_text(GTK_EDITABLE(test_self->category_name_entry),
                        "symbols & stuff!!");
  g_assert_true(gtk_widget_get_sensitive(test_self->category_save_button));
  window_test_teardown();
}

static void test_manage_search_entry(void) {
  window_test_setup();
  GtkWidget *page =
      gtk_stack_get_child_by_name(test_self->view_stack, "manage");
  g_assert_nonnull(page);
  GtkWidget *search = g_object_get_data(G_OBJECT(page), "manage-search");
  g_assert_nonnull(search);
  g_assert_true(GTK_IS_SEARCH_ENTRY(search));
  window_test_teardown();
}

static void test_manage_populated(void) {
  window_test_setup();
  g_action_group_activate_action(G_ACTION_GROUP(test_self), "manage", NULL);
  GtkWidget *page =
      gtk_stack_get_child_by_name(test_self->view_stack, "manage");
  GtkWidget *content = g_object_get_data(G_OBJECT(page), "manage-content");
  g_assert_nonnull(content);
  g_assert_nonnull(gtk_widget_get_first_child(content));
  window_test_teardown();
}

static void test_manage_dupe_name_blocked(void) {
  window_test_setup();
  g_assert_cmpuint(test_self->category_widgets->len, >, 0);
  CategoryWidgets *cw = g_ptr_array_index(test_self->category_widgets, 0);
  gtk_editable_set_text(GTK_EDITABLE(test_self->category_name_entry), cw->name);
  g_assert_false(gtk_widget_get_sensitive(test_self->category_save_button));
  window_test_teardown();
}

int main(int argc, char *argv[]) {
  g_setenv("MESON_SOURCE_ROOT", SRCDIR, TRUE);
  g_test_init(&argc, &argv, NULL);

  run_find_kaomoji_bogus();

  gboolean have_display = gtk_init_check();

  g_test_add_func("/display-name/underscores", test_display_name_underscores);
  g_test_add_func("/display-name/no-underscores",
                  test_display_name_no_underscores);
  g_test_add_func("/display-name/already-upper",
                  test_display_name_already_upper);
  g_test_add_func("/display-name/single-char", test_display_name_single_char);
  g_test_add_func("/display-name/empty", test_display_name_empty);
  g_test_add_func("/find-kaomoji/with-env", test_find_kaomoji_with_env);
  g_test_add_func("/find-kaomoji/bogus", test_find_kaomoji_bogus);
  g_test_add_func("/sni/category", test_sni_category);
  g_test_add_func("/sni/id", test_sni_id);
  g_test_add_func("/sni/item-is-menu", test_sni_item_is_menu);
  g_test_add_func("/sni/menu", test_sni_menu);
  g_test_add_func("/sni/unknown", test_sni_unknown);
  g_test_add_func("/dbusmenu/version", test_dbusmenu_version);
  g_test_add_func("/dbusmenu/status", test_dbusmenu_status);
  g_test_add_func("/dbusmenu/text-direction", test_dbusmenu_text_direction);
  g_test_add_func("/dbusmenu/unknown", test_dbusmenu_unknown);
  g_test_add_func("/category-widgets/chip-init",
                  test_category_widgets_chip_init);
  if (have_display) {
    g_test_add_func("/window/chip-setup", test_window_chip_setup);
    g_test_add_func("/window/chip-labels-match", test_window_chip_labels_match);
    g_test_add_func("/window/initial-active-chip",
                    test_window_initial_active_chip);
    g_test_add_func("/window/all-categories-visible",
                    test_window_all_categories_visible);
    g_test_add_func("/window/bottom-spacer", test_window_bottom_spacer);
    g_test_add_func("/window/view-stack-exists", test_window_view_stack_exists);
    g_test_add_func("/window/add-button-navigates",
                    test_window_add_button_navigates);
    g_test_add_func("/window/stack-pages-exist", test_window_stack_pages_exist);
    g_test_add_func("/window/category-name-validation",
                    test_category_name_validation);
    g_test_add_func("/window/add-category-creates-dir",
                    test_add_category_creates_dir);
    g_test_add_func("/window/add-entry-creates-file",
                    test_add_entry_creates_file);
    g_test_add_func("/window/export-creates-archive",
                    test_export_creates_archive);
    g_test_add_func("/window/import-extracts-and-loads",
                    test_import_extracts_and_loads);
    g_test_add_func("/window/manage-search-entry", test_manage_search_entry);
    g_test_add_func("/window/manage-populated", test_manage_populated);
    g_test_add_func("/window/manage-dupe-name-blocked",
                    test_manage_dupe_name_blocked);
  }

  return g_test_run();
}

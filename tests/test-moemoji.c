#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include "moemoji-internal.h"
#include "moemoji-window.h"

static void
test_display_name_underscores (void)
{
    char *name = make_display_name ("happy_faces");
    g_assert_cmpstr (name, ==, "Happy faces");
    g_free (name);
}

static void
test_display_name_no_underscores (void)
{
    char *name = make_display_name ("animals");
    g_assert_cmpstr (name, ==, "Animals");
    g_free (name);
}

static void
test_display_name_already_upper (void)
{
    char *name = make_display_name ("Animals");
    g_assert_cmpstr (name, ==, "Animals");
    g_free (name);
}

static void
test_display_name_single_char (void)
{
    char *name = make_display_name ("x");
    g_assert_cmpstr (name, ==, "X");
    g_free (name);
}

static void
test_display_name_empty (void)
{
    char *name = make_display_name ("");
    g_assert_cmpstr (name, ==, "");
    g_free (name);
}

static void
test_find_kaomoji_with_env (void)
{
    char *dir = find_kaomoji_dir ();
    g_assert_nonnull (dir);
    g_assert_true (g_file_test (dir, G_FILE_TEST_IS_DIR));
    g_free (dir);
}

static gboolean find_kaomoji_bogus_passed = FALSE;

static void
run_find_kaomoji_bogus (void)
{
    g_setenv ("MESON_SOURCE_ROOT", "/nonexistent", TRUE);
    char *saved = g_get_current_dir ();
    g_assert_true (g_chdir ("/tmp") == 0);

    char *dir = find_kaomoji_dir ();
    g_free (dir);

    g_assert_true (g_chdir (saved) == 0);
    g_free (saved);
    g_setenv ("MESON_SOURCE_ROOT", SRCDIR, TRUE);
    find_kaomoji_bogus_passed = TRUE;
}

static void
test_find_kaomoji_bogus (void)
{
    g_assert_true (find_kaomoji_bogus_passed);
}

static void
test_sni_category (void)
{
    GVariant *v = sni_get_property (NULL, NULL, NULL, NULL, "Category", NULL, NULL);
    g_assert_nonnull (v);
    g_assert_cmpstr (g_variant_get_string (v, NULL), ==, "ApplicationStatus");
    g_variant_unref (v);
}

static void
test_sni_id (void)
{
    GVariant *v = sni_get_property (NULL, NULL, NULL, NULL, "Id", NULL, NULL);
    g_assert_nonnull (v);
    g_assert_cmpstr (g_variant_get_string (v, NULL), ==, "moemoji");
    g_variant_unref (v);
}

static void
test_sni_item_is_menu (void)
{
    GVariant *v = sni_get_property (NULL, NULL, NULL, NULL, "ItemIsMenu", NULL, NULL);
    g_assert_nonnull (v);
    g_assert_false (g_variant_get_boolean (v));
    g_variant_unref (v);
}

static void
test_sni_menu (void)
{
    GVariant *v = sni_get_property (NULL, NULL, NULL, NULL, "Menu", NULL, NULL);
    g_assert_nonnull (v);
    g_assert_cmpstr (g_variant_get_string (v, NULL), ==, "/MenuBar");
    g_variant_unref (v);
}

static void
test_sni_unknown (void)
{
    GVariant *v = sni_get_property (NULL, NULL, NULL, NULL, "Nonexistent", NULL, NULL);
    g_assert_null (v);
}

static void
test_dbusmenu_version (void)
{
    GVariant *v = dbusmenu_get_property (NULL, NULL, NULL, NULL, "Version", NULL, NULL);
    g_assert_nonnull (v);
    g_assert_cmpuint (g_variant_get_uint32 (v), ==, 3);
    g_variant_unref (v);
}

static void
test_dbusmenu_status (void)
{
    GVariant *v = dbusmenu_get_property (NULL, NULL, NULL, NULL, "Status", NULL, NULL);
    g_assert_nonnull (v);
    g_assert_cmpstr (g_variant_get_string (v, NULL), ==, "normal");
    g_variant_unref (v);
}

static void
test_dbusmenu_text_direction (void)
{
    GVariant *v = dbusmenu_get_property (NULL, NULL, NULL, NULL, "TextDirection", NULL, NULL);
    g_assert_nonnull (v);
    g_assert_cmpstr (g_variant_get_string (v, NULL), ==, "ltr");
    g_variant_unref (v);
}

static void
test_dbusmenu_unknown (void)
{
    GVariant *v = dbusmenu_get_property (NULL, NULL, NULL, NULL, "Bogus", NULL, NULL);
    g_assert_null (v);
}

static void
test_category_widgets_chip_init (void)
{
    CategoryWidgets *cw = g_new0 (CategoryWidgets, 1);
    g_assert_null (cw->chip);
    g_assert_null (cw->header);
    g_assert_null (cw->flow);
    g_assert_null (cw->name);
    g_free (cw);
}

G_GNUC_INTERNAL GResource *moemoji_get_resource (void);

static GResource *test_res = NULL;
static GtkWidget *test_win = NULL;
static MoeMojiWindow *test_self = NULL;

static void
window_test_setup (void)
{
    if (!test_res) {
        test_res = moemoji_get_resource ();
        g_resources_register (test_res);
    }
    test_win = g_object_new (MOEMOJI_TYPE_WINDOW, NULL);
    test_self = MOEMOJI_WINDOW (test_win);
}

static void
window_test_teardown (void)
{
    gtk_window_destroy (GTK_WINDOW (test_win));
    test_win = NULL;
    test_self = NULL;
}

static void
test_window_chip_setup (void)
{
    window_test_setup ();

    g_assert_cmpuint (test_self->category_widgets->len, >, 0);

    for (guint i = 0; i < test_self->category_widgets->len; i++) {
        CategoryWidgets *cw = g_ptr_array_index (test_self->category_widgets, i);
        g_assert_nonnull (cw->chip);
    }

    window_test_teardown ();
}

static void
test_window_chip_labels_match (void)
{
    window_test_setup ();

    for (guint i = 0; i < test_self->category_widgets->len; i++) {
        CategoryWidgets *cw = g_ptr_array_index (test_self->category_widgets, i);
        const char *chip_label = gtk_button_get_label (GTK_BUTTON (cw->chip));
        g_assert_cmpstr (chip_label, ==, cw->name);
    }

    window_test_teardown ();
}

static void
test_window_initial_active_chip (void)
{
    window_test_setup ();

    g_assert_cmpint (test_self->active_chip_index, ==, -1);

    window_test_teardown ();
}

static void
test_window_all_categories_visible (void)
{
    window_test_setup ();

    for (guint i = 0; i < test_self->category_widgets->len; i++) {
        CategoryWidgets *cw = g_ptr_array_index (test_self->category_widgets, i);
        g_assert_true (gtk_widget_get_visible (cw->header));
        g_assert_true (gtk_widget_get_visible (cw->flow));
    }

    window_test_teardown ();
}

static void
test_window_bottom_spacer (void)
{
    window_test_setup ();

    g_assert_nonnull (test_self->bottom_spacer);
    GtkWidget *last = gtk_widget_get_last_child (GTK_WIDGET (test_self->content_box));
    g_assert_true (last == test_self->bottom_spacer);

    window_test_teardown ();
}

int
main (int argc, char *argv[])
{
    g_setenv ("MESON_SOURCE_ROOT", SRCDIR, TRUE);
    g_test_init (&argc, &argv, NULL);

    run_find_kaomoji_bogus ();

    gboolean have_display = gtk_init_check ();

    g_test_add_func ("/display-name/underscores",       test_display_name_underscores);
    g_test_add_func ("/display-name/no-underscores",    test_display_name_no_underscores);
    g_test_add_func ("/display-name/already-upper",     test_display_name_already_upper);
    g_test_add_func ("/display-name/single-char",       test_display_name_single_char);
    g_test_add_func ("/display-name/empty",             test_display_name_empty);
    g_test_add_func ("/find-kaomoji/with-env",          test_find_kaomoji_with_env);
    g_test_add_func ("/find-kaomoji/bogus",             test_find_kaomoji_bogus);
    g_test_add_func ("/sni/category",                   test_sni_category);
    g_test_add_func ("/sni/id",                         test_sni_id);
    g_test_add_func ("/sni/item-is-menu",               test_sni_item_is_menu);
    g_test_add_func ("/sni/menu",                       test_sni_menu);
    g_test_add_func ("/sni/unknown",                    test_sni_unknown);
    g_test_add_func ("/dbusmenu/version",               test_dbusmenu_version);
    g_test_add_func ("/dbusmenu/status",                test_dbusmenu_status);
    g_test_add_func ("/dbusmenu/text-direction",        test_dbusmenu_text_direction);
    g_test_add_func ("/dbusmenu/unknown",               test_dbusmenu_unknown);
    g_test_add_func ("/category-widgets/chip-init",     test_category_widgets_chip_init);
    if (have_display) {
        g_test_add_func ("/window/chip-setup",               test_window_chip_setup);
        g_test_add_func ("/window/chip-labels-match",        test_window_chip_labels_match);
        g_test_add_func ("/window/initial-active-chip",      test_window_initial_active_chip);
        g_test_add_func ("/window/all-categories-visible",   test_window_all_categories_visible);
        g_test_add_func ("/window/bottom-spacer",            test_window_bottom_spacer);
    }

    return g_test_run ();
}

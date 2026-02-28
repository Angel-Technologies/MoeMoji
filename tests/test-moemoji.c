#include <glib.h>
#include <glib/gstdio.h>
#include "moemoji-internal.h"

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
    g_setenv ("MESON_SOURCE_ROOT", SRCDIR, TRUE);
    char *dir = find_kaomoji_dir ();
    g_assert_nonnull (dir);
    g_assert_true (g_file_test (dir, G_FILE_TEST_IS_DIR));
    g_free (dir);
    g_unsetenv ("MESON_SOURCE_ROOT");
}

static void
test_find_kaomoji_bogus (void)
{
    g_setenv ("MESON_SOURCE_ROOT", "/nonexistent", TRUE);
    char *saved = g_get_current_dir ();
    g_assert_true (g_chdir ("/tmp") == 0);

    char *dir = find_kaomoji_dir ();
    g_free (dir);

    g_assert_true (g_chdir (saved) == 0);
    g_free (saved);
    g_unsetenv ("MESON_SOURCE_ROOT");
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

int
main (int argc, char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/display-name/underscores",     test_display_name_underscores);
    g_test_add_func ("/display-name/no-underscores",   test_display_name_no_underscores);
    g_test_add_func ("/display-name/already-upper",    test_display_name_already_upper);
    g_test_add_func ("/display-name/single-char",      test_display_name_single_char);
    g_test_add_func ("/display-name/empty",            test_display_name_empty);
    g_test_add_func ("/find-kaomoji/with-env",         test_find_kaomoji_with_env);
    g_test_add_func ("/find-kaomoji/bogus",            test_find_kaomoji_bogus);
    g_test_add_func ("/sni/category",                  test_sni_category);
    g_test_add_func ("/sni/id",                        test_sni_id);
    g_test_add_func ("/sni/item-is-menu",              test_sni_item_is_menu);
    g_test_add_func ("/sni/menu",                      test_sni_menu);
    g_test_add_func ("/sni/unknown",                   test_sni_unknown);
    g_test_add_func ("/dbusmenu/version",              test_dbusmenu_version);
    g_test_add_func ("/dbusmenu/status",               test_dbusmenu_status);
    g_test_add_func ("/dbusmenu/text-direction",       test_dbusmenu_text_direction);
    g_test_add_func ("/dbusmenu/unknown",              test_dbusmenu_unknown);

    return g_test_run ();
}

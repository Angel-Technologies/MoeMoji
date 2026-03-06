#pragma once

#include <gtk/gtk.h>

typedef struct {
  GtkWidget *header;
  GtkWidget *flow;
  GtkWidget *chip;
  char *name;
} CategoryWidgets;

struct _MoeMojiWindow {
  GtkApplicationWindow parent_instance;
  GtkBox *wrapper_box;
  GtkBox *outer_box;
  GtkBox *content_box;
  GtkSearchEntry *search_entry;
  GtkWidget *header_bar;
  GtkBox *category_bar;
  GtkWidget *kaomoji_scroll;
  GPtrArray *category_widgets;
  GtkWidget *bottom_spacer;
  int active_chip_index;
  GtkStack *view_stack;
  GtkWidget *add_button;
  GtkWidget *sort_button;
  GtkWidget *back_button;
  GtkWidget *menu_button;
  GSettings *settings;
  GtkEntry *category_name_entry;
  GtkWidget *entry_text_view;
  GtkWidget *category_save_button;
  char *selected_category_dir;
  gulong scroll_handler_id;
  gulong page_size_handler_id;
};

G_BEGIN_DECLS

#define MOEMOJI_TYPE_WINDOW (moemoji_window_get_type())

G_DECLARE_FINAL_TYPE(MoeMojiWindow, moemoji_window, MOEMOJI, WINDOW,
                     GtkApplicationWindow);

G_END_DECLS

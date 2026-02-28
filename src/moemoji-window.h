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
  GtkBox *outer_box;
  GtkBox *content_box;
  GtkSearchEntry *search_entry;
  GtkWidget *header_bar;
  GtkBox *category_bar;
  GtkWidget *kaomoji_scroll;
  GPtrArray *category_widgets;
  GtkWidget *bottom_spacer;
  int active_chip_index;
};

G_BEGIN_DECLS

#define MOEMOJI_TYPE_WINDOW (moemoji_window_get_type())

G_DECLARE_FINAL_TYPE(MoeMojiWindow, moemoji_window, MOEMOJI, WINDOW,
                     GtkApplicationWindow);

G_END_DECLS

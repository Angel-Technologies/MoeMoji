#pragma once

#include <adwaita.h>

typedef struct {
  GtkWidget *header;
  GtkWidget *flow;
  GtkWidget *chip;
  char *name;
  char *path;
} CategoryWidgets;

struct _MoeMojiWindow {
  GtkApplicationWindow parent_instance;
  GtkPaned *paned;
  GtkBox *right_pane;
  GtkBox *outer_box;
  GtkBox *content_box;
  GtkSearchEntry *search_entry;
  GtkWidget *header_bar;
  GtkBox *category_bar;
  GtkWidget *sidebar_toggle;
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
  GtkWidget *category_error_label;
  char *selected_category_dir;
  gulong scroll_handler_id;
  gulong page_size_handler_id;
  char *ctx_cat_path;
  char *ctx_cat_name;
  char *ctx_emote_path;
  char *ctx_emote_text;
  GtkBox *pinned_box;
  AdwToastOverlay *toast_overlay;
};

G_BEGIN_DECLS

#define MOEMOJI_TYPE_WINDOW (moemoji_window_get_type())

G_DECLARE_FINAL_TYPE(MoeMojiWindow, moemoji_window, MOEMOJI, WINDOW,
                     GtkApplicationWindow);

G_END_DECLS

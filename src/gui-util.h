/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_GUI_UTIL_H_
# define _GNM_GUI_UTIL_H_

#include "gnumeric-fwd.h"
#include <goffice/goffice.h>
#include "numbers.h"
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GNM_ACTION_DEF(name)			\
	void name (GtkAction *a, WBCGtk *wbcg)

GtkWidget* gnumeric_go_error_info_dialog_create (GOErrorInfo *error);
GtkWidget* gnumeric_go_error_info_list_dialog_create (GSList *errs);
void       gnumeric_go_error_info_dialog_show (GtkWindow *parent,
					       GOErrorInfo *error);
void       gnumeric_go_error_info_list_dialog_show (GtkWindow *parent,
						    GSList *errs);
void       gnumeric_restore_window_geometry (GtkWindow *dialog,
					     const char *key);
void       gnumeric_keyed_dialog (WBCGtk *wbcg,
				  GtkWindow *dialog,
				  char const *key);
gpointer   gnumeric_dialog_raise_if_exists (WBCGtk *wbcg,
					    char const *key);
void       gnumeric_editable_enters	(GtkWindow *window, GtkWidget *editable);

/* Utility routine as Gtk does not have any decent routine to do this */
int gnm_gtk_radio_group_get_selected (GSList *radio_group);
/* Utility routine as libglade does not have any decent routine to do this */
int gnm_gui_group_value (gpointer gui, char const * const group[]);

/* Use this on menus that are popped up */
void gnumeric_popup_menu (GtkMenu *menu, GdkEvent *event);

/*
 * Pseudo-tooltip support code.
 */
void        gnumeric_position_tooltip (GtkWidget *tip, int px, int py,
				       gboolean horizontal);
GtkWidget  *gnumeric_create_tooltip (GtkWidget *ref_widget);
GtkWidget  *gnumeric_convert_to_tooltip (GtkWidget *ref_widget,
					 GtkWidget *widget);

GtkBuilder *gnm_gtk_builder_load (char const *uifile, char const *domain,
				 GOCmdContext *cc);

typedef struct {
	char const *name;
	char const *pixmap;
	int display_filter;
	int sensitive_filter;

	int index;

	char *allocated_name;
} GnumericPopupMenuElement;

typedef void (*GnumericPopupMenuHandler) (GnumericPopupMenuElement const *e,
					  gpointer user_data);

void gnumeric_create_popup_menu (GnumericPopupMenuElement const *elements,
				 GnumericPopupMenuHandler handler,
				 gpointer user_data,
				 int display_filter,
				 int sensitive_filter,
				 GdkEvent *event);

#define gnumeric_filter_modifiers(a) ((a) &(~(GDK_LOCK_MASK|GDK_MOD2_MASK|GDK_MOD5_MASK)))

void gnumeric_init_help_button	(GtkWidget *w, char const *link);

char *gnumeric_textbuffer_get_text (GtkTextBuffer *buf);
char *gnumeric_textview_get_text (GtkTextView *text_view);
void  gnumeric_textview_set_text (GtkTextView *text_view, char const *txt);
void  gnm_load_pango_attributes_into_buffer (PangoAttrList  *markup,
					     GtkTextBuffer *buffer,
					     gchar const *str);
PangoAttrList *gnm_get_pango_attributes_from_buffer (GtkTextBuffer *buffer);

void focus_on_entry (GtkEntry *entry);

/* WARNING : These do not handle dates correctly
 * We should be passing in a DateConvention */
#define entry_to_float(entry, the_float, update)	\
	entry_to_float_with_format (entry, the_float, update, NULL)
gboolean entry_to_float_with_format (GtkEntry *entry,
				     gnm_float *the_float,
				     gboolean update,
				     GOFormat const *format);
gboolean entry_to_float_with_format_default (GtkEntry *entry,
					     gnm_float *the_float,
					     gboolean update,
					     GOFormat const *format,
					     gnm_float num);
gboolean entry_to_int	(GtkEntry *entry, gint *the_int, gboolean update);
void	 float_to_entry	(GtkEntry *entry, gnm_float the_float);
void	 int_to_entry	(GtkEntry *entry, gint the_int);

GtkWidget *gnumeric_load_image  (char const *name);
GdkPixbuf *gnumeric_load_pixbuf (char const *name);

void gnm_link_button_and_entry (GtkWidget *button, GtkWidget *entry);

void gnm_widget_set_cursor_type (GtkWidget *w, GdkCursorType ct);
void gnm_widget_set_cursor (GtkWidget *w, GdkCursor *ct);

int gnm_widget_measure_string (GtkWidget *w, const char *s);

GtkWidget * gnumeric_message_dialog_create (GtkWindow * parent,
                                            GtkDialogFlags flags,
                                            GtkMessageType type,
                                            char const *primary_message,
                                            char const *secondary_message);

typedef gboolean (*gnm_iter_search_t) (GtkTreeModel *model, GtkTreeIter* iter);
#define gnm_tree_model_iter_next gtk_tree_model_iter_next
gboolean gnm_tree_model_iter_prev (GtkTreeModel *model, GtkTreeIter* iter);

typedef enum {
	GNM_DIALOG_DESTROY_SHEET_ADDED = 0x01,
	GNM_DIALOG_DESTROY_SHEET_REMOVED = 0x02,
	GNM_DIALOG_DESTROY_SHEET_RENAMED = 0x04,
	GNM_DIALOG_DESTROY_SHEETS_REORDERED = 0x08,
	GNM_DIALOG_DESTROY_CURRENT_SHEET_REMOVED = 0x100,
	GNM_DIALOG_DESTROY_CURRENT_SHEET_RENAMED = 0x200
} GnmDialogDestroyOptions;

void gnm_dialog_setup_destroy_handlers (GtkDialog *dialog,
					WBCGtk *wbcg,
					GnmDialogDestroyOptions what);

void gnm_canvas_get_position (GocCanvas *canvas, int *x, int *y,
			      gint64 px, gint64 py);
void gnm_canvas_get_screen_position (GocCanvas *canvas,
				     double x, double y,
				     int *ix, int *iy);

gboolean gnm_check_for_plugins_missing (char const **ids, GtkWindow *parent);

void
gnm_cell_renderer_text_copy_background_to_cairo (GtkCellRendererText *crt,
						 cairo_t *cr);

G_END_DECLS

#endif /* _GNM_GUI_UTIL_H_ */

/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_GUI_UTIL_H_
# define _GNM_GUI_UTIL_H_

#include "gui-gnumeric.h"
#include "command-context.h"
#include "gutils.h"

#include <goffice/app/error-info.h>
#include "numbers.h"
#include <goffice/gtk/goffice-gtk.h>
#include <gtk/gtkbutton.h>
#include <gtk/gtkcombo.h>
#include <gtk/gtkfilesel.h>
#include <gtk/gtkmessagedialog.h>
#include <gtk/gtkmenu.h>
#include <gtk/gtktoolbar.h>
#include <gtk/gtktextview.h>
#include <gtk/gtkentry.h>
#include <glade/glade-xml.h>

G_BEGIN_DECLS

#define GNM_ACTION_DEF(name)			\
	void name (GtkAction *a, WBCGtk *wbcg)

GtkWidget* gnumeric_error_info_dialog_new (ErrorInfo *error);
void       gnumeric_error_info_dialog_show (GtkWindow *parent,
                                            ErrorInfo *error);
void       gnumeric_keyed_dialog (WBCGtk *wbcg,
				  GtkWindow *dialog,
				  char const *key);
gpointer   gnumeric_dialog_raise_if_exists (WBCGtk *wbcg,
					    char const *key);
void       gnumeric_editable_enters	(GtkWindow *window, GtkWidget *w);

/* Utility routine as Gtk does not have any decent routine to do this */
int gtk_radio_group_get_selected (GSList *radio_group);
/* Utility routine as libglade does not have any decent routine to do this */
int gnumeric_glade_group_value (GladeXML *gui, char const * const group[]);

/* Use this on menus that are popped up */
void gnumeric_popup_menu (GtkMenu *menu, GdkEventButton *event);

/*
 * Pseudo-tool-tip support code.
 */
void        gnumeric_position_tooltip (GtkWidget *tip, int horizontal);
GtkWidget  *gnumeric_create_tooltip (void);

GladeXML   *gnm_glade_xml_new (GOCmdContext *cc, char const *gladefile,
			       char const *root, char const *domain);

typedef struct {
	char const *name;
	char const *pixmap;
	int display_filter;
	int sensitive_filter;

	int index;
} GnumericPopupMenuElement;

typedef gboolean (*GnumericPopupMenuHandler) (GnumericPopupMenuElement const *e,
					      gpointer user_data);

void gnumeric_create_popup_menu (GnumericPopupMenuElement const *elements,
				 GnumericPopupMenuHandler handler,
				 gpointer user_data,
				 int display_filter,
				 int sensitive_filter,
				 GdkEventButton *event);

#define gnumeric_filter_modifiers(a) ((a) &(~(GDK_LOCK_MASK|GDK_MOD2_MASK|GDK_MOD5_MASK)))

GnmColor *go_combo_color_get_style_color (GtkWidget *color_combo);

void gnumeric_init_help_button	(GtkWidget *w, char const *link);

char *gnumeric_textview_get_text (GtkTextView *text_view);
void  gnumeric_textview_set_text (GtkTextView *text_view, char const *txt);

void focus_on_entry (GtkEntry *entry);

/* WARNING : These do not handle dates correctly
 * We should be passing in a DateConvention */
#define entry_to_float(entry, the_float, update)	\
	entry_to_float_with_format (entry, the_float, update, NULL)
gboolean entry_to_float_with_format (GtkEntry *entry, gnm_float *the_float, gboolean update,
				     GOFormat *format);
gboolean entry_to_float_with_format_default (GtkEntry *entry, gnm_float *the_float, gboolean update,
					     GOFormat *format, gnm_float num);
gboolean entry_to_int	(GtkEntry *entry, gint *the_int, gboolean update);
void	 float_to_entry	(GtkEntry *entry, gnm_float the_float);
void	 int_to_entry	(GtkEntry *entry, gint the_int);

GtkWidget *gnumeric_load_image  (char const *name);
GdkPixbuf *gnumeric_load_pixbuf (char const *name);

void gnm_link_button_and_entry (GtkWidget *button, GtkWidget *entry);

void gnm_widget_set_cursor_type (GtkWidget *w, GdkCursorType ct);
void gnm_widget_set_cursor (GtkWidget *w, GdkCursor *ct);

GtkWidget * gnumeric_message_dialog_new (GtkWindow * parent,
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
	GNM_DIALOG_DESTROY_CURRENT_SHEET_RENAMED = 0x200,
} GnmDialogDestroyOptions;

void gnm_dialog_setup_destroy_handlers (GtkDialog *dialog,
					WBCGtk *wbcg,
					GnmDialogDestroyOptions what);

G_END_DECLS

#endif /* _GNM_GUI_UTIL_H_ */

#ifndef GNUMERIC_GUI_UTIL_H
#define GNUMERIC_GUI_UTIL_H

#include "workbook-control-gui.h"
#include "error-info.h"
#include "gutils.h"
#include <libgnomeui/libgnomeui.h>
#include <glade/glade-xml.h>

gboolean   gnumeric_dialog_question_yes_no (WorkbookControlGUI *wbcg,
                                            const gchar *message,
                                            gboolean default_answer);
gboolean   gnumeric_dialog_file_selection (WorkbookControlGUI *wbcg, GtkFileSelection *fsel);
gboolean   gnumeric_dialog_dir_selection (WorkbookControlGUI *wbcg, GtkFileSelection *fsel);
void       gnumeric_notice (WorkbookControlGUI *wbcg, GtkMessageType type, const char *str);
void       gnumeric_notice_nonmodal (GtkWindow *parent, GtkWidget **ref,
				     GtkMessageType type, char const *str);

void       gnumeric_non_modal_dialog (WorkbookControlGUI *wbcg, GtkWindow *dialog);
gint       gnumeric_dialog_run  (WorkbookControlGUI *wbcg, GtkDialog *dialog);
void       gnumeric_error_info_dialog_show (WorkbookControlGUI *wbcg,
                                            ErrorInfo *error);
void       gnumeric_set_transient (WorkbookControlGUI *context, GtkWindow *window);
void       gnumeric_keyed_dialog (WorkbookControlGUI *wbcg,
				  GtkWindow *dialog,
				  const char *key);
gpointer   gnumeric_dialog_raise_if_exists (WorkbookControlGUI *wbcg,
					    const char *key);
void       gnumeric_editable_enters (GtkWindow *window, GtkWidget *w);
void       gnumeric_combo_enters (GtkWindow *window, GtkCombo *combo);
void       gnumeric_toolbar_insert_with_eventbox (GtkToolbar *toolbar,
                                                  GtkWidget  *widget,
                                                  const char *tooltip_text,
                                                  const char *tooltip_private_text,
                                                  gint        position);
void       gnumeric_toolbar_append_with_eventbox (GtkToolbar *toolbar,
                                                  GtkWidget  *widget,
                                                  const char *tooltip_text,
                                                  const char *tooltip_private_text);

/* change alignment of a stock buttons content (should be removed for 2.2) */
void gtk_button_stock_alignment_set (GtkButton *button,
				     gfloat     xalign,
				     gfloat     yalign,
				     gfloat     xscale,
				     gfloat     yscale);

/* Utility routine as Gtk does not have any decent routine to do this */
int gtk_radio_group_get_selected (GSList *radio_group);
/* Utility routine as libglade does not have any decent routine to do this */
int gnumeric_glade_group_value (GladeXML *gui, const char *group[]);

/* Use this on menus that are popped up */
void gnumeric_popup_menu (GtkMenu *menu, GdkEventButton *event);

/* Scroll the viewing area of the list to the given row */
void gnumeric_clist_moveto (GtkCList *clist, gint row);

/* Get the selected index of an option menu */
int  gnumeric_option_menu_get_selected_index (GtkOptionMenu *optionmenu);

/*
 * Pseudo-tool-tip support code.
 */
void        gnumeric_position_tooltip (GtkWidget *tip, int horizontal);
GtkWidget  *gnumeric_create_tooltip (void);

GladeXML   *gnumeric_glade_xml_new (WorkbookControlGUI *context, char const * gladefile);

void 	    gnumeric_inject_widget_into_bonoboui (WorkbookControlGUI *wbcg,
						  GtkWidget *widget,
						  char const *path);

typedef struct {
	char const * name;
	char const * pixmap;
	int display_filter;
	int sensitive_filter;

	int index;
} GnumericPopupMenuElement;

typedef gboolean (*GnumericPopupMenuHandler) (GnumericPopupMenuElement const *e,
					      gpointer user_data);

void gnumeric_create_popup_menu_list (GSList *elements,
				      GnumericPopupMenuHandler handler,
				      gpointer user_data,
				      int display_filter,
				      int sensitive_filter,
				      GdkEventButton *event);

void gnumeric_create_popup_menu (GnumericPopupMenuElement const *elements,
				 GnumericPopupMenuHandler handler,
				 gpointer user_data,
				 int display_filter,
				 int sensitive_filter,
				 GdkEventButton *event);

#define gnumeric_filter_modifiers(a) ((a) &(~(GDK_LOCK_MASK|GDK_MOD2_MASK|GDK_MOD5_MASK)))

StyleColor *color_combo_get_style_color (GtkWidget *color_combo);

GtkWidget *gnumeric_toolbar_new (WorkbookControlGUI *wbcg, GnomeUIInfo *info,
				  char const *name, gint band_num, gint band_position, gint offset);
GtkWidget *gnumeric_toolbar_get_widget (GtkToolbar *toolbar, int pos);

void gnumeric_help_display	(char const *link);
void gnumeric_init_help_button	(GtkWidget *w, char const *link);
void gnumeric_pbox_init_help	(GtkWidget *dialog, char const *link);

char *gnumeric_textview_get_text (GtkTextView *text_view);
void  gnumeric_textview_set_text (GtkTextView *text_view, char const *txt);

void focus_on_entry (GtkEntry *entry);

#define entry_to_float(entry, the_float, update)	\
	entry_to_float_with_format (entry, the_float, update, NULL)
gboolean entry_to_float_with_format (GtkEntry *entry, gnum_float *the_float, gboolean update,
				     StyleFormat *format);
gboolean entry_to_float_with_format_default (GtkEntry *entry, gnum_float *the_float, gboolean update,
					     StyleFormat *format, gnum_float num);
gboolean entry_to_int	(GtkEntry *entry, gint *the_int, gboolean update);
void	 float_to_entry	(GtkEntry *entry, gnum_float the_float);
void	 int_to_entry	(GtkEntry *entry, gint the_int);

GtkWidget *gnumeric_load_image  (char const *name);
GdkPixbuf *gnumeric_load_pixbuf (char const *name);

#endif /* GNUMERIC_GUI_UTIL_H */

#ifndef GNUMERIC_GUI_UTIL_H
#define GNUMERIC_GUI_UTIL_H

#include <libgnomeui/libgnomeui.h>
#include <glade/glade-xml.h>
#include "workbook-control-gui.h"
#include "error-info.h"
#include "gutils.h"

gboolean   gnumeric_dialog_question_yes_no (WorkbookControlGUI *wbcg,
                                            const gchar *message,
                                            gboolean default_answer);
gboolean   gnumeric_dialog_file_selection (WorkbookControlGUI *wbcg, GtkFileSelection *fsel);
void       gnumeric_notice (WorkbookControlGUI *wbcg, const char *type, const char *str);

void       gnumeric_non_modal_dialog (WorkbookControlGUI *wbcg, GtkWindow *dialog);
gint       gnumeric_dialog_run  (WorkbookControlGUI *wbcg, GnomeDialog *dialog);
void       gnumeric_dialog_show (WorkbookControlGUI *wbcg,
				 GnomeDialog *dialog,
                                 gboolean click_closes,
                                 gboolean close_with_parent);
void       gnumeric_error_info_dialog_show (WorkbookControlGUI *wbcg,
                                            ErrorInfo *error);
void       gnumeric_set_transient (WorkbookControlGUI *context, GtkWindow *window);
void       gnumeric_keyed_dialog (WorkbookControlGUI *wbcg,
				  GtkWindow *dialog,
				  const char *key);
gboolean   gnumeric_dialog_raise_if_exists (WorkbookControlGUI *wbcg,
					    char *key);
void       gnumeric_editable_enters (GtkWindow *window, GtkEditable *editable);
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

/* A simple routine for making a popup menu */
int        run_popup_menu  (GdkEvent *event, int button, char **strings);

/* Utility routine as Gtk does not have any decent routine to do this */
int        gtk_radio_group_get_selected (GSList *radio_group);
/* Utility routine as libglade does not have any decent routine to do this */
int        gnumeric_glade_group_value (GladeXML *gui, const char *group[]);

char      *x11_font_get_bold_name       (const char *fontname, int units);
char      *x11_font_get_italic_name     (const char *fontname, int units);
char      *x11_font_change_component    (const char *fontname, int idx, const char *value);

/*
 * Use this on menus that are popped up
 */
void      gnumeric_auto_kill_popup_menu_on_hide (GtkMenu *menu);
void      gnumeric_popup_menu                   (GtkMenu *menu, GdkEventButton *event);

/* Scroll the viewing area of the list to the given row */
void gnumeric_clist_moveto (GtkCList *clist, gint row);

/* Scroll the viewing area of the list to the first selected row */
void gnumeric_clist_make_selection_visible (GtkCList *clist);

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

void gnumeric_create_popup_menu (GnumericPopupMenuElement const *elements,
				 GnumericPopupMenuHandler handler,
				 gpointer user_data,
				 int display_filter,
				 int sensitive_filter,
				 GdkEventButton *event);

#define gnumeric_filter_modifiers(a) ((a) &(~(GDK_LOCK_MASK|GDK_MOD2_MASK|GDK_MOD5_MASK)))

StyleColor * color_combo_get_style_color (GtkWidget *color_combo);

#endif /* GNUMERIC_GUI_UTIL_H */

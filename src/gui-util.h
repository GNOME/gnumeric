#ifndef GNUMERIC_GNUMERIC_UTIL_H
#define GNUMERIC_GNUMERIC_UTIL_H

#include <gnome.h>
#include <glade/glade-xml.h>
#include "gnumeric.h"

void       gnumeric_notice (Workbook *wb, const char *type, const char *str);
void       gnumeric_no_modify_array_notice (Workbook *wb);

void       gnumeric_non_modal_dialog (Workbook *wb, GtkWindow *dialog);
gint       gnumeric_dialog_run (Workbook *wb, GnomeDialog *dialog);
void       gnumeric_dialog_show (GtkObject *parent, GnomeDialog *dialog,
				 gboolean click_closes,
				 gboolean close_with_parent);
void       gnumeric_set_transient (CommandContext *context, GtkWindow *window);
void       gnumeric_editable_enters (GtkWindow *window,
				     GtkEditable *editable);
void       gnumeric_combo_enters (GtkWindow *window,
				  GtkCombo *combo);
void
gnumeric_toolbar_insert_with_eventbox (GtkToolbar *toolbar, GtkWidget  *widget,
				       const char *tooltip_text,
				       const char *tooltip_private_text,
				       gint        position);
void
gnumeric_toolbar_append_with_eventbox (GtkToolbar *toolbar, GtkWidget  *widget,
				       const char *tooltip_text,
				       const char *tooltip_private_text);
				  
/* A simple routine for making a popup menu */
int        run_popup_menu  (GdkEvent *event, int button, char **strings);

/* Utility routine as Gtk does not have any decent routine to do this */
int        gtk_radio_group_get_selected (GSList *radio_group);
void       gtk_radio_button_select      (GSList *group, int n);

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

/* Is this GtkEntry editing at a subexpression boundary */
gboolean    gnumeric_entry_at_subexpr_boundary_p (GtkEntry *entry);

GladeXML   *gnumeric_glade_xml_new (CommandContext *context, char const * gladefile);

void 	    gnumeric_inject_widget_into_bonoboui (Workbook *wb,
						  GtkWidget *widget,
						  char const *path);

#endif /* GNUMERIC_GNUMERIC_UTIL_H */

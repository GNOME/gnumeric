#ifndef GNUMERIC_GNUMERIC_UTIL_H
#define GNUMERIC_GNUMERIC_UTIL_H

#include <gnome.h>
#include <glade/glade-xml.h>
#include "gnumeric.h"

void       gnumeric_notice (Workbook *wb, const char *type, const char *str);
void       gnumeric_no_modify_array_notice (Workbook *wb);

gint       gnumeric_dialog_run (Workbook *wb, GnomeDialog *dialog);
void       gnumeric_dialog_show (GtkWidget *parent, GnomeDialog *dialog,
				 gboolean click_closes,
				 gboolean close_with_parent);
void       gnumeric_set_transient (CommandContext *context, GtkWindow *window);
void       gnumeric_editable_enters (GtkWindow *window,
				     GtkEditable *editable);

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

/*
 * Pseudo-tool-tip support code.
 */
void        gnumeric_position_tooltip (GtkWidget *tip, int horizontal);
GtkWidget  *gnumeric_create_tooltip (void);

/* Is this GtkEntry editing at a subexpression boundary */
gboolean    gnumeric_entry_at_subexpr_boundary_p (GtkWidget const * const w);

GladeXML   *gnumeric_glade_xml_new (CommandContext *context, char const * gladefile);

#endif /* GNUMERIC_GNUMERIC_UTIL_H */

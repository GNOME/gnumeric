#ifndef GNUMERIC_GNUMERIC_UTIL_H
#define GNUMERIC_GNUMERIC_UTIL_H

void       gnumeric_notice (Workbook *wb, char *type, char *str);

/* A simple routine for making a popup menu */
int        run_popup_menu  (GdkEvent *event, int button, char **strings);

/* Utility routine as Gtk does not have any decent routine to do this */
int        gtk_radio_group_get_selected (GSList *radio_group);
void       gtk_radio_button_select      (GSList *group, int n);

char      *font_get_bold_name           (char *fontname);
char      *font_get_italic_name         (char *fontname);
char      *font_change_component        (char *fontname, int idx, char *value);

/*
 * Use this on menus that are popped up
 */
void      gnumeric_auto_kill_popup_menu_on_hide (GtkMenu *menu);
void      gnumeric_popup_menu                   (GtkMenu *menu, GdkEventButton *event);

#endif /* GNUMERIC_GNUMERIC_UTIL_H */

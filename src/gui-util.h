#ifndef GNUMERIC_UTIL_H
#define GNUMERIC_UTIL_H

void       gnumeric_notice (char *str);

/* Utility routine as Gtk does not have any decent routine to do this */
int        gtk_radio_group_get_selected (GSList *radio_group);
void       gtk_radio_button_select      (GSList *group, int n);

#endif

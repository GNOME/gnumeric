#ifndef GNUMERIC_UTILS_DIALOG_H
#define GNUMERIC_UTILS_DIALOG_H

GtkWidget *gnumeric_dialog_entry_new (GnomeDialog *dialog);

GtkWidget *gnumeric_dialog_entry_new_with_max_length (GnomeDialog *dialog, guint16 max);

GtkWidget *
hbox_pack_label_and_entry(char *str, char *default_str,
			  int entry_len, GtkWidget *vbox);

GList *add_strings_to_glist          (const char *strs[]);

#endif



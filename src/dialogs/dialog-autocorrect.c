/*
 * dialog-autocorrect.c:
 *
 * Author:
 *        Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *
 * (C) Copyright 2000, 2001 by Jukka-Pekka Iivonen <iivonen@iki.fi>
 **/

#include <config.h>
#include <glib.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-help.h>
#include <glade/glade.h>
#include <ctype.h>
#include "gnumeric.h"
#include "gui-util.h"
#include "dialogs.h"
#include "commands.h"
#include "workbook.h"
#include "workbook-control.h"
#include "dialog-autocorrect.h"


typedef struct {
        GtkWidget       *dia;
        Workbook        *wb;
        WorkbookControlGUI *wbcg;
} autocorrect_t;

typedef struct {
        GtkWidget *entry;
        GtkWidget *list;
} exceptions_t;


gboolean   autocorrect_init_caps;
gboolean   autocorrect_first_letter;
gboolean   autocorrect_names_of_days;
gboolean   autocorrect_caps_lock;
gboolean   autocorrect_replace;
GList      *autocorrect_fl_exceptions;
GList      *autocorrect_in_exceptions;
gint       fl_row, in_row;

/* Add the name of the days on your language if they are always capitalized.
 */
static char *autocorrect_day [] = {
        /* English */
        "monday", "tuesday", "wednesday", "thursday",
	"friday", "saturday", "sunday", NULL
};

char *
autocorrect_tool (const char *command)
{
        unsigned char *s;
	unsigned char *ucommand = (unsigned char *)g_strdup (command);
	gint i, len;

	len = strlen (ucommand);

        if (autocorrect_init_caps) {
		for (s = ucommand; *s; s++) {
		skip_ic_correct:
		        if (isupper (*s) && isupper (s[1])) {
			        if (islower (s[2])) {
				        GList *c = autocorrect_in_exceptions;
					while (c != NULL) {
					        guchar *a = (guchar *)c->data;
					        if (strncmp (s, a, strlen (a))
						    == 0) {
						        s++;
						        goto skip_ic_correct;
						}
						c = c->next;
					}
				        s[1] = tolower (s[1]);
				} else
				        while (!isspace(*s))
					        ++s;
			}
		}
	}

	if (autocorrect_first_letter) {
	        unsigned char *p;

	        for (s = ucommand; *s; s = p+1) {
		skip_first_letter:
		        p = strchr(s, '.');
			if (p == NULL)
			        break;
			while (isspace(*s))
			        ++s;
			if (islower (*s) && (s == ucommand || isspace (s[-1]))) {
			        GList *cur = autocorrect_fl_exceptions;

				for ( ; cur != NULL; cur = cur->next) {
				        guchar *t, *c = (guchar *)cur->data;
					gint  l = strlen (c);
					gint  spaces = 0;

					for (t = s - 1; t >= ucommand; t--)
					        if (isspace (*t))
						        ++spaces;
						else
						        break;
				        if (s - ucommand > l + spaces &&
					    strncmp(s-l-spaces, c, l) == 0) {
					        s = p + 1;
					        goto skip_first_letter;
					}
				}
			        *s = toupper (*s);
			}
		}
	}

	if (autocorrect_names_of_days) {
	        for (i = 0; autocorrect_day[i] != NULL; i++) {
		        do {
			        s = strstr (ucommand, autocorrect_day[i]);
				if (s != NULL)
				        *s = toupper (*s);
			} while (s != NULL);
		}
	}

	if (autocorrect_caps_lock) {
	        if (len > 1 && islower (ucommand[0]) && isupper (ucommand[1]))
		        for (i = 0; i < len; i++)
			        if (isalpha (ucommand[i])) {
				        if (isupper (ucommand[i]))
					        ucommand[i] = tolower (ucommand[i]);
					else
					        ucommand[i] = toupper (ucommand[i]);
				}
	}

	return ucommand;
}

static void
init_caps_toggled(GtkWidget *widget, gpointer ignored)
{
        autocorrect_init_caps = GTK_TOGGLE_BUTTON (widget)->active;
}

static void
first_letter_toggled(GtkWidget *widget, gpointer ignored)
{
        autocorrect_first_letter = GTK_TOGGLE_BUTTON (widget)->active;
}

static void
names_of_days_toggled(GtkWidget *widget, gpointer ignored)
{
        autocorrect_names_of_days = GTK_TOGGLE_BUTTON (widget)->active;
}

static void
caps_lock_toggled(GtkWidget *widget, gpointer ignored)
{
        autocorrect_caps_lock = GTK_TOGGLE_BUTTON (widget)->active;
}

static void
replace_toggled(GtkWidget *widget, gpointer ignored)
{
        autocorrect_replace = GTK_TOGGLE_BUTTON (widget)->active;
}

static void
add_fl_clicked (GtkWidget *widget, exceptions_t *p)
{
        gchar    *s[2], *txt, *str;
	GList    *current;
	gboolean new_flag = TRUE;

	txt = gtk_entry_get_text (GTK_ENTRY (p->entry));
	for (current = autocorrect_fl_exceptions; current != NULL;
	     current = current->next) {
	        gchar *x = (gchar *) current->data;

	        if (strcmp(x, txt) == 0) {
		        new_flag = FALSE;
			break;
		}
	}

	if (new_flag) {
	        gint row;

	        s[0] = txt;
		s[1] = NULL;
		str = g_strdup (txt);
		row = gtk_clist_append(GTK_CLIST (p->list), s);
		gtk_clist_set_row_data (GTK_CLIST (p->list), row, str);
		autocorrect_fl_exceptions =
		  g_list_prepend (autocorrect_fl_exceptions, (gpointer) str);
	}
	gtk_entry_set_text (GTK_ENTRY (p->entry), "");
}

static void
add_in_clicked (GtkWidget *widget, exceptions_t *p)
{
        gchar    *s[2], *txt, *str;
	GList    *current;
	gboolean new_flag = TRUE;

	txt = gtk_entry_get_text (GTK_ENTRY (p->entry));
	for (current = autocorrect_in_exceptions; current != NULL;
	     current = current->next) {
	        gchar *x = (gchar *) current->data;

	        if (strcmp(x, txt) == 0) {
		        new_flag = FALSE;
			break;
		}
	}

	if (new_flag) {
	        gint row;

	        s[0] = txt;
		s[1] = NULL;
		str = g_strdup (txt);
		row = gtk_clist_append(GTK_CLIST (p->list), s);
		gtk_clist_set_row_data (GTK_CLIST (p->list), row, str);
		autocorrect_in_exceptions =
		  g_list_prepend (autocorrect_in_exceptions, (gpointer) str);
	}
	gtk_entry_set_text (GTK_ENTRY (p->entry), "");
}

static void
remove_fl_clicked (GtkWidget *widget, exceptions_t *p)
{
        if (fl_row >= 0) {
	        gpointer x = gtk_clist_get_row_data (GTK_CLIST (p->list),
						     fl_row);
	        gtk_clist_remove (GTK_CLIST (p->list), fl_row);
		autocorrect_fl_exceptions =
		  g_list_remove (autocorrect_fl_exceptions, x);
	}
}

static void
remove_in_clicked (GtkWidget *widget, exceptions_t *p)
{
        if (fl_row >= 0) {
	        gpointer x = gtk_clist_get_row_data (GTK_CLIST (p->list),
						     in_row);
	        gtk_clist_remove (GTK_CLIST (p->list), in_row);
		autocorrect_in_exceptions =
		  g_list_remove (autocorrect_in_exceptions, x);
	}
}

static void
fl_select_row (GtkWidget *widget, gint row, gint col, GdkEventButton *event,
	       gpointer data)
{
        fl_row = row;
}

static void
in_select_row (GtkWidget *widget, gint row, gint col, GdkEventButton *event,
	       gpointer data)
{
        in_row = row;
}

static void
exceptions_callback (GtkWidget *widget, autocorrect_t *p)
{
	GtkWidget *dia;
	GtkWidget *add;
	GtkWidget *remove;
	GList     *cur;
	gint      v;
	exceptions_t e1, e2;
        GladeXML  *gui;

	gui = gnumeric_glade_xml_new (p->wbcg, "autocorrect-exceptions.glade");
        if (gui == NULL)
                return;

	dia = glade_xml_get_widget (gui, "AutoCorrectExceptions");

	fl_row = -1;
	if (!dia) {
	        printf("Corrupt file autocorrect-exceptions.glade\n");
		return;
	}

	gtk_widget_hide (p->dia);

	e1.entry = glade_xml_get_widget (gui, "entry1");
	e1.list = glade_xml_get_widget (gui, "clist1");

	/* Make <Ret> in entry fields invoke default */
	gnome_dialog_editable_enters (GNOME_DIALOG (dia),
				      GTK_EDITABLE (e1.entry));
	gtk_signal_connect(GTK_OBJECT(e1.list), "select_row",
			   GTK_SIGNAL_FUNC(fl_select_row), NULL);

	e2.entry = glade_xml_get_widget (gui, "entry2");
	e2.list = glade_xml_get_widget (gui, "clist2");

	/* Make <Ret> in entry fields invoke default */
	gnome_dialog_editable_enters (GNOME_DIALOG (dia),
				      GTK_EDITABLE (e2.entry));
	gtk_signal_connect(GTK_OBJECT(e2.list), "select_row",
			   GTK_SIGNAL_FUNC(in_select_row), NULL);

	for (cur = autocorrect_fl_exceptions; cur != NULL; cur = cur->next) {
	        gchar *s[2], *txt = (gchar *) cur->data;
		gint  row;

	        s[0] = txt;
		s[1] = NULL;
		row = gtk_clist_append(GTK_CLIST (e1.list), s);
		gtk_clist_set_row_data (GTK_CLIST (e1.list), row, cur->data);
	}

	for (cur = autocorrect_in_exceptions; cur != NULL; cur = cur->next) {
	        gchar *s[2], *txt = (gchar *) cur->data;
		gint  row;

	        s[0] = txt;
		s[1] = NULL;
		row = gtk_clist_append(GTK_CLIST (e2.list), s);
		gtk_clist_set_row_data (GTK_CLIST (e2.list), row, cur->data);
	}

	add = glade_xml_get_widget (gui, "button4");
	gtk_signal_connect (GTK_OBJECT (add), "clicked",
			    GTK_SIGNAL_FUNC (add_fl_clicked), &e1);

	remove = glade_xml_get_widget (gui, "button5");
	gtk_signal_connect (GTK_OBJECT (remove), "clicked",
			    GTK_SIGNAL_FUNC (remove_fl_clicked), &e1);

	add = glade_xml_get_widget (gui, "button6");
	gtk_signal_connect (GTK_OBJECT (add), "clicked",
			    GTK_SIGNAL_FUNC (add_in_clicked), &e2);

	remove = glade_xml_get_widget (gui, "button7");
	gtk_signal_connect (GTK_OBJECT (remove), "clicked",
			    GTK_SIGNAL_FUNC (remove_in_clicked), &e2);

	v = gnumeric_dialog_run (p->wbcg, GNOME_DIALOG (dia));
	if (v != -1)
	        gtk_object_destroy (GTK_OBJECT (dia));
	gtk_object_unref (GTK_OBJECT (gui));
	gtk_widget_show (p->dia);
}

static void
dialog_help_cb(GtkWidget *button, gchar *helpfile)
{
        if (helpfile != NULL) {
	        GnomeHelpMenuEntry help_ref;
                help_ref.name = "gnumeric";
                help_ref.path = helpfile;
                gnome_help_display (NULL, &help_ref);
	}
}

/*
 *  Widgets for "replace text when typed" have been set insensitive in
 *  autocorrect.glade until the feature is implemented. The widgets are:
 *  checkbutton5, label1, label2, entry1, entry2.
 *
 */
void
dialog_autocorrect (WorkbookControlGUI *wbcg)
{
	GladeXML  *gui;
	GtkWidget *dia;
	GtkWidget *exceptions;
	GtkWidget *init_caps;
	GtkWidget *first_letter;
	GtkWidget *names_of_days;
	GtkWidget *caps_lock;
	GtkWidget *replace;
	GtkWidget *entry;
	GtkWidget *helpbutton;
	gchar     *helpfile = "autocorrect.html";
	autocorrect_t p;

	gint      v;
	gint      old_init_caps, old_first_letter, old_names_of_days;
	gint      old_caps_lock, old_replace;

	old_init_caps = autocorrect_init_caps;
	old_first_letter = autocorrect_first_letter;
	old_names_of_days = autocorrect_names_of_days;
	old_caps_lock = autocorrect_caps_lock;
	old_replace = autocorrect_replace;

	gui = gnumeric_glade_xml_new (wbcg, "autocorrect.glade");
        if (gui == NULL)
                return;

	dia = glade_xml_get_widget (gui, "AutoCorrect");
	if (!dia) {
		printf ("Corrupt file autocorrect.glade\n");
		return;
	}

	p.wb = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	p.wbcg = wbcg;
	p.dia = dia;

	exceptions = glade_xml_get_widget (gui, "Exceptions");
	gtk_signal_connect (GTK_OBJECT (exceptions), "clicked",
			    GTK_SIGNAL_FUNC (exceptions_callback), &p);

	init_caps = glade_xml_get_widget (gui, "checkbutton1");
	if (autocorrect_init_caps)
	        gtk_toggle_button_set_active ((GtkToggleButton *)
					      init_caps,
					      autocorrect_init_caps);
	gtk_signal_connect (GTK_OBJECT (init_caps), "toggled",
			    GTK_SIGNAL_FUNC (init_caps_toggled), NULL);


	first_letter = glade_xml_get_widget (gui, "checkbutton2");
	if (autocorrect_first_letter)
	        gtk_toggle_button_set_active ((GtkToggleButton *)
					      first_letter,
					      autocorrect_first_letter);
	gtk_signal_connect (GTK_OBJECT (first_letter), "toggled",
			    GTK_SIGNAL_FUNC (first_letter_toggled), NULL);

	names_of_days = glade_xml_get_widget (gui, "checkbutton3");
	if (autocorrect_names_of_days)
	        gtk_toggle_button_set_active ((GtkToggleButton *)
					      names_of_days,
					      autocorrect_names_of_days);
	gtk_signal_connect (GTK_OBJECT (names_of_days), "toggled",
			    GTK_SIGNAL_FUNC (names_of_days_toggled), NULL);

	caps_lock = glade_xml_get_widget (gui, "checkbutton4");
	if (autocorrect_caps_lock)
	        gtk_toggle_button_set_active ((GtkToggleButton *)
					      caps_lock,
					      autocorrect_caps_lock);
	gtk_signal_connect (GTK_OBJECT (caps_lock), "toggled",
			    GTK_SIGNAL_FUNC (caps_lock_toggled), NULL);

	replace = glade_xml_get_widget (gui, "checkbutton5");
	gtk_widget_set_sensitive (replace, FALSE);

	if (autocorrect_replace)
	        gtk_toggle_button_set_active ((GtkToggleButton *)
					      replace,
					      autocorrect_replace);
	gtk_signal_connect (GTK_OBJECT (replace), "toggled",
			    GTK_SIGNAL_FUNC (replace_toggled), NULL);

        helpbutton = glade_xml_get_widget(gui, "helpbutton");
        gtk_signal_connect (GTK_OBJECT (helpbutton), "clicked",
                            GTK_SIGNAL_FUNC (dialog_help_cb), helpfile);

	/* Make <Ret> in entry fields invoke default */
	entry = glade_xml_get_widget (gui, "entry1");
	gtk_widget_set_sensitive (entry, FALSE);
	gnome_dialog_editable_enters (GNOME_DIALOG (dia),
				      GTK_EDITABLE (entry));
	entry = glade_xml_get_widget (gui, "entry2");
	gnome_dialog_editable_enters (GNOME_DIALOG (dia),
				      GTK_EDITABLE (entry));
	gtk_widget_set_sensitive (entry, FALSE);

 loop:
	v = gnumeric_dialog_run (wbcg, GNOME_DIALOG (dia));

        if (v == 2)
                goto loop;

	if (v != 0) {
	        autocorrect_init_caps = old_init_caps;
		autocorrect_first_letter = old_first_letter;
		autocorrect_names_of_days = old_names_of_days;
		autocorrect_caps_lock = old_caps_lock;
		autocorrect_replace = old_replace;
	}

	if (v != -1)
		gtk_object_destroy (GTK_OBJECT (dia));

	gtk_object_unref (GTK_OBJECT (gui));
}

/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * dialog-autocorrect.c:
 *
 * Author:
 *        Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *
 * (C) Copyright 2000, 2001 by Jukka-Pekka Iivonen <iivonen@iki.fi>
 **/
#include <gnumeric-config.h>
#include <gnumeric.h>
#include "dialogs.h"

#include <auto-correct.h>
#include <gui-util.h>
#include <workbook-edit.h>

#include <libgnome/gnome-i18n.h>
#include <glade/glade.h>

typedef struct {
	gboolean   changed;
	gint       row;
        GtkWidget *entry;
        GtkWidget *list;
	GList	  *exceptions;
} AutoCorrectExceptionState;

typedef struct {
	GladeXML  	   *glade;
        GtkWidget          *dialog;
	GtkWidget	   *cancel_button;
        Workbook           *wb;
        WorkbookControlGUI *wbcg;

	gboolean  features [AC_MAX_FEATURE];
	AutoCorrectExceptionState init_caps, first_letter;
} AutoCorrectState;

static void
cb_add_clicked (GtkWidget *widget, AutoCorrectExceptionState *s)
{
	gchar const *txt;
        gchar    *dumy[2], *str;
	GList    *ptr;
	gboolean new_flag = TRUE;

	txt = gtk_entry_get_text (GTK_ENTRY (s->entry));
	for (ptr = s->exceptions; ptr != NULL; ptr = ptr->next) {
	        gchar *x = (gchar *) ptr->data;

	        if (strcmp(x, txt) == 0) {
		        new_flag = FALSE;
			break;
		}
	}

	if (new_flag) {
	        gint row;

	        dumy[0] = (char *)txt;
		dumy[1] = NULL;
		str = g_strdup (txt);
		row = gtk_clist_append(GTK_CLIST (s->list), dumy);
		gtk_clist_set_row_data (GTK_CLIST (s->list), row, str);
		s->exceptions = g_list_prepend (s->exceptions, str);
		s->changed = TRUE;
	}
	gtk_entry_set_text (GTK_ENTRY (s->entry), "");
}

static void
cb_remove_clicked (GtkWidget *widget, AutoCorrectExceptionState *s)
{
        if (s->row >= 0) {
	        gpointer x = gtk_clist_get_row_data (GTK_CLIST (s->list),
						     s->row);
	        gtk_clist_remove (GTK_CLIST (s->list), s->row);
		s->exceptions = g_list_remove (s->exceptions, x);
		g_free (x);
		s->changed = TRUE;
	}
}

static void
cb_select_row (GtkWidget *widget, gint row, gint col, GdkEventButton *event,
	       AutoCorrectExceptionState *s)
{
        s->row = row;
}

static void
autocorrect_init_exception_list (AutoCorrectState *state,
				 AutoCorrectExceptionState *exception,
				 GList *exceptions,
				 char const *entry_name,
				 char const *list_name,
				 char const *add_name,
				 char const *remove_name)
{
	GtkWidget *w;
	GList     *ptr;

	exception->changed = FALSE;
	exception->row = -1;
	exception->exceptions = exceptions;
	exception->entry = glade_xml_get_widget (state->glade, entry_name);
	exception->list = glade_xml_get_widget (state->glade, list_name);
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
		GTK_EDITABLE (exception->entry));
	gtk_signal_connect (GTK_OBJECT (exception->list), "select_row",
		GTK_SIGNAL_FUNC (cb_select_row), exception);
	for (ptr = exceptions; ptr != NULL; ptr = ptr->next) {
	        gchar *s[2], *txt = (gchar *) ptr->data;
		gint  row;

	        s[0] = txt;
		s[1] = NULL;
		row = gtk_clist_append(GTK_CLIST (exception->list), s);
		gtk_clist_set_row_data (GTK_CLIST (exception->list), row, txt);
	}

	w = glade_xml_get_widget (state->glade, add_name);
	gtk_signal_connect (GTK_OBJECT (w), "clicked",
		GTK_SIGNAL_FUNC (cb_add_clicked), exception);
	w = glade_xml_get_widget (state->glade, remove_name);
	gtk_signal_connect (GTK_OBJECT (w), "clicked",
		GTK_SIGNAL_FUNC (cb_remove_clicked), exception);
}

static void
ac_button_toggled (GtkWidget *widget, gpointer flag)
{
	*((gboolean *)flag) = GTK_TOGGLE_BUTTON (widget)->active;
}

static void
ac_dialog_toggle_init (AutoCorrectState *state, char const *name,
		       AutoCorrectFeature f)
{
	GtkWidget *w = glade_xml_get_widget (state->glade, name);

	g_return_if_fail (w != NULL);

	state->features [f] = autocorrect_get_feature (f);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w),
		state->features [f]);
	gtk_signal_connect (GTK_OBJECT (w),
		"toggled",
		GTK_SIGNAL_FUNC (ac_button_toggled), state->features + f);
}

static gboolean
cb_autocorrect_destroy (GtkObject *w, AutoCorrectState *state)
{
	if (state->glade != NULL) {
		gtk_object_unref (GTK_OBJECT (state->glade));
		state->glade = NULL;
	}

	if (state->wbcg != NULL)
		wbcg_edit_detach_guru (state->wbcg);
	state->dialog = NULL;
	g_free (state);

	return TRUE;
}

static  gint
cb_autocorrect_key_press (GtkWidget *widget, GdkEventKey *event,
			  AutoCorrectState *state)
{
	if (event->keyval == GDK_Escape) {
		gtk_object_destroy (GTK_OBJECT (state->dialog));
		return TRUE;
	} else
		return FALSE;
}

static void
cb_dialog_help (GtkWidget *button, gchar *link)
{
	gnumeric_help_display (link);
}

static void
cb_button_clicked (GtkWidget *button, AutoCorrectState *state)
{
	if (state->cancel_button != button) {
		int i;
		if (state->init_caps.changed)
			autocorrect_set_exceptions (AC_INIT_CAPS,
				state->init_caps.exceptions);
		if (state->first_letter.changed)
			autocorrect_set_exceptions (AC_FIRST_LETTER,
				state->first_letter.exceptions);
		for (i = 0 ; i < AC_MAX_FEATURE ; i++)
			autocorrect_set_feature (i, state->features [i]);
		autocorrect_store_config ();
	}
	gtk_object_destroy (GTK_OBJECT (state->dialog));
}

static gboolean
dialog_init (AutoCorrectState *state)
{
	GtkWidget *entry;
	GtkWidget *button;

	state->glade = gnumeric_glade_xml_new (state->wbcg, "autocorrect.glade");
        if (state->glade == NULL)
                return TRUE;
	state->dialog = glade_xml_get_widget (state->glade, "AutoCorrect");
	if (state->dialog == NULL) {
		g_warning ("Corrupt file autocorrect.glade\n");
		return TRUE;
	}

	state->wb = wb_control_workbook (WORKBOOK_CONTROL (state->wbcg));
	ac_dialog_toggle_init (state, "init_caps",     AC_INIT_CAPS);
	ac_dialog_toggle_init (state, "first_letter",  AC_FIRST_LETTER);
	ac_dialog_toggle_init (state, "names_of_days", AC_NAMES_OF_DAYS);
	ac_dialog_toggle_init (state, "replace_text",  AC_REPLACE);

        button = glade_xml_get_widget (state->glade, "help_button");
        gtk_signal_connect (GTK_OBJECT (button), "clicked",
		GTK_SIGNAL_FUNC (cb_dialog_help), "autocorrect-tool.html");
        button = glade_xml_get_widget (state->glade, "ok_button");
        gtk_signal_connect (GTK_OBJECT (button), "clicked",
		GTK_SIGNAL_FUNC (cb_button_clicked), state);
        button = glade_xml_get_widget (state->glade, "cancel_button");
        gtk_signal_connect (GTK_OBJECT (button), "clicked",
		GTK_SIGNAL_FUNC (cb_button_clicked), state);
	state->cancel_button = button;

	/* Make <Ret> in entry fields invoke default */
	entry = glade_xml_get_widget (state->glade, "entry1");
	gtk_widget_set_sensitive (entry, FALSE);
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
		GTK_EDITABLE (entry));
	entry = glade_xml_get_widget (state->glade, "entry2");
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
		GTK_EDITABLE (entry));
	gtk_widget_set_sensitive (entry, FALSE);

	autocorrect_init_exception_list (state, &state->init_caps,
		autocorrect_get_exceptions (AC_INIT_CAPS),
		"init_caps_entry", "init_caps_list",
		"init_caps_add", "init_caps_remove");
	autocorrect_init_exception_list (state, &state->first_letter,
		autocorrect_get_exceptions (AC_FIRST_LETTER),
		"first_letter_entry", "first_letter_list",
		"first_letter_add", "first_letter_remove");

	gtk_signal_connect (GTK_OBJECT (state->dialog),
		"destroy",
		GTK_SIGNAL_FUNC (cb_autocorrect_destroy), state);
	gtk_signal_connect (GTK_OBJECT (state->dialog),
		"key_press_event",
		GTK_SIGNAL_FUNC (cb_autocorrect_key_press), state);
	wbcg_edit_attach_guru (state->wbcg, state->dialog);

	return FALSE;
}

#define AUTO_CORRECT_KEY	"AutoCorrect"

void
dialog_autocorrect (WorkbookControlGUI *wbcg)
{
	AutoCorrectState *state;

	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));

	if (gnumeric_dialog_raise_if_exists (wbcg, AUTO_CORRECT_KEY))
		return;

	state = g_new (AutoCorrectState, 1);
	state->wbcg = wbcg;
	state->glade = NULL;
	state->init_caps.exceptions = NULL;
	state->first_letter.exceptions = NULL;

	if (dialog_init (state)) {
		gnumeric_notice (wbcg, GTK_MESSAGE_ERROR,
				 _("Could not create the AutoCorrect dialog."));
		cb_autocorrect_destroy (NULL, state);
		return;
	}

	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       AUTO_CORRECT_KEY);

	gtk_widget_show (state->dialog);
}

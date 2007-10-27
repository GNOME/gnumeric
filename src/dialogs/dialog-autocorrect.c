/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * dialog-autocorrect.c:
 *
 * Author:
 *        Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *
 * (C) Copyright 2000, 2001 by Jukka-Pekka Iivonen <iivonen@iki.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 **/


/*
 *  FIXME:  since we are displaying gconf data, we should register a notification and
 *          update our info on gconf changes!
 *
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include "dialogs.h"

#include <auto-correct.h>
#include <gui-util.h>
#include <wbc-gtk.h>

#include <glade/glade.h>
#include <gsf/gsf-impl-utils.h>
#include <gtk/gtk.h>
#include <goffice/utils/go-glib-extras.h>
#include <string.h>
#include "help.h"

typedef struct {
	gboolean	 changed;
        GtkWidget	*entry;
        GtkWidget	*list;
	GtkListStore	*model;
	GSList		*exceptions;
} AutoCorrectExceptionState;

typedef struct {
	GladeXML	   *gui;
        GtkWidget          *dialog;
        Workbook           *wb;
        WBCGtk *wbcg;

	gboolean  features [AC_MAX_FEATURE];
	AutoCorrectExceptionState init_caps, first_letter;
} AutoCorrectState;

static void
cb_add_clicked (G_GNUC_UNUSED GtkWidget *widget,
		AutoCorrectExceptionState *s)
{
	gchar const *txt;
	GSList      *ptr;
	GtkTreeIter  iter;
	gboolean     new_flag = TRUE;

	txt = gtk_entry_get_text (GTK_ENTRY (s->entry));
	for (ptr = s->exceptions; ptr != NULL; ptr = ptr->next)
		if (strcmp (ptr->data, txt) == 0) {
			new_flag = FALSE;
			break;
		}

	if (new_flag) {
		gchar *tmp = g_strdup (txt);
		gtk_list_store_append (s->model, &iter);
		gtk_list_store_set (s->model, &iter, 0, tmp, -1);
		s->exceptions = g_slist_prepend (s->exceptions, tmp);
		s->changed = TRUE;
	}
	gtk_entry_set_text (GTK_ENTRY (s->entry), "");
}

static void
cb_remove_clicked (G_GNUC_UNUSED GtkWidget *widget,
		   AutoCorrectExceptionState *s)
{
	char *txt;
	GtkTreeIter iter;
	GtkTreeSelection *selection =
		gtk_tree_view_get_selection (GTK_TREE_VIEW (s->list));

	if (!gtk_tree_selection_get_selected (selection, NULL, &iter))
		return;

	gtk_tree_model_get (GTK_TREE_MODEL (s->model), &iter,
		0, &txt,
		-1);
	s->exceptions = g_slist_delete_link
		(s->exceptions,
		 g_slist_find_custom (s->exceptions, txt,
				      (GCompareFunc)strcmp));
	gtk_list_store_remove (s->model, &iter);
	g_free (txt);
	s->changed = TRUE;
}

static void
autocorrect_init_exception_list (AutoCorrectState *state,
				 AutoCorrectExceptionState *exception,
				 GSList *exceptions,
				 char const *entry_name,
				 char const *list_name,
				 char const *add_name,
				 char const *remove_name)
{
	GtkWidget   *w;
	GtkTreeIter  iter;
	GtkTreeSelection *selection;

	exception->changed = FALSE;
	exception->exceptions = exceptions;
	exception->entry = glade_xml_get_widget (state->gui, entry_name);
	exception->model = gtk_list_store_new (1, G_TYPE_STRING);
	exception->list = glade_xml_get_widget (state->gui, list_name);
	gtk_tree_view_set_model (GTK_TREE_VIEW (exception->list),
				 GTK_TREE_MODEL (exception->model));
	gtk_tree_view_append_column (GTK_TREE_VIEW (exception->list),
		gtk_tree_view_column_new_with_attributes (NULL,
			gtk_cell_renderer_text_new (),
			"text", 0,
			NULL));

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (exception->list));
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);

	for (; exceptions != NULL; exceptions = exceptions->next) {
		gtk_list_store_append (exception->model, &iter);
		gtk_list_store_set (exception->model, &iter,
			0,	 exceptions->data,
			-1);
	}

	w = glade_xml_get_widget (state->gui, add_name);
	gtk_button_set_alignment (GTK_BUTTON (w), 0., .5);
	g_signal_connect (G_OBJECT (w),
		"clicked",
		G_CALLBACK (cb_add_clicked), exception);
	w = glade_xml_get_widget (state->gui, remove_name);
	gtk_button_set_alignment (GTK_BUTTON (w), 0., .5);
	g_signal_connect (G_OBJECT (w),
		"clicked",
		G_CALLBACK (cb_remove_clicked), exception);
	g_signal_connect (G_OBJECT (exception->entry),
		"activate",
		G_CALLBACK (cb_add_clicked), exception);

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
	GtkWidget *w = glade_xml_get_widget (state->gui, name);

	g_return_if_fail (w != NULL);

	state->features [f] = autocorrect_get_feature (f);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w),
		state->features [f]);
	g_signal_connect (GTK_OBJECT (w),
		"toggled",
		G_CALLBACK (ac_button_toggled), state->features + f);
}

static void
cb_autocorrect_destroy (AutoCorrectState *state)
{
	go_slist_free_custom (state->init_caps.exceptions, g_free);
	state->init_caps.exceptions = NULL;

	go_slist_free_custom (state->first_letter.exceptions, g_free);
	state->first_letter.exceptions = NULL;

	if (state->gui != NULL)
		g_object_unref (G_OBJECT (state->gui));
	g_free (state);
}

static void
cb_cancel_button_clicked (G_GNUC_UNUSED GtkWidget *button,
			  AutoCorrectState *state)
{
	gtk_object_destroy (GTK_OBJECT (state->dialog));
}

static void
cb_apply_button_clicked (G_GNUC_UNUSED GtkWidget *button,
			 AutoCorrectState *state)
{
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

static void
cb_ok_button_clicked (GtkWidget *button, AutoCorrectState *state)
{
	cb_apply_button_clicked (button, state);
	gtk_object_destroy (GTK_OBJECT (state->dialog));

}

static gboolean
dialog_init (AutoCorrectState *state)
{
	GtkWidget *entry;
	GtkWidget *button;

	state->dialog = glade_xml_get_widget (state->gui, "AutoCorrect");
	if (state->dialog == NULL) {
		g_warning ("Corrupt file autocorrect.glade");
		return TRUE;
	}

	state->wb = wb_control_get_workbook (WORKBOOK_CONTROL (state->wbcg));
	ac_dialog_toggle_init (state, "init_caps",     AC_INIT_CAPS);
	ac_dialog_toggle_init (state, "first_letter",  AC_FIRST_LETTER);
	ac_dialog_toggle_init (state, "names_of_days", AC_NAMES_OF_DAYS);
	ac_dialog_toggle_init (state, "replace_text",  AC_REPLACE);

        button = glade_xml_get_widget (state->gui, "help_button");
	gnumeric_init_help_button (button, GNUMERIC_HELP_LINK_AUTOCORRECT);

        button = glade_xml_get_widget (state->gui, "ok_button");
        g_signal_connect (GTK_OBJECT (button),
		"clicked",
		G_CALLBACK (cb_ok_button_clicked), state);
        button = glade_xml_get_widget (state->gui, "apply_button");
        g_signal_connect (GTK_OBJECT (button),
		"clicked",
		G_CALLBACK (cb_apply_button_clicked), state);
        button = glade_xml_get_widget (state->gui, "cancel_button");
        g_signal_connect (GTK_OBJECT (button),
		"clicked",
		G_CALLBACK (cb_cancel_button_clicked), state);

	/* Make <Ret> in entry fields invoke default */
	entry = glade_xml_get_widget (state->gui, "entry1");
	gtk_widget_set_sensitive (entry, FALSE);
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
		GTK_WIDGET (entry));
	entry = glade_xml_get_widget (state->gui, "entry2");
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
		GTK_WIDGET (entry));
	gtk_widget_set_sensitive (entry, FALSE);

	autocorrect_init_exception_list (state, &state->init_caps,
		autocorrect_get_exceptions (AC_INIT_CAPS),
		"init_caps_entry", "init_caps_list",
		"init_caps_add", "init_caps_remove");
	autocorrect_init_exception_list (state, &state->first_letter,
		autocorrect_get_exceptions (AC_FIRST_LETTER),
		"first_letter_entry", "first_letter_list",
		"first_letter_add", "first_letter_remove");

	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify) cb_autocorrect_destroy);

	return FALSE;
}

#define AUTO_CORRECT_KEY	"AutoCorrect"

void
dialog_autocorrect (WBCGtk *wbcg)
{
	AutoCorrectState *state;
	GladeXML *gui;

	g_return_if_fail (IS_WBC_GTK (wbcg));

	if (gnumeric_dialog_raise_if_exists (wbcg, AUTO_CORRECT_KEY))
		return;
	gui = gnm_glade_xml_new (GO_CMD_CONTEXT (wbcg),
		"autocorrect.glade", NULL, NULL);
        if (gui == NULL)
                return;

	state = g_new (AutoCorrectState, 1);
	state->wbcg = wbcg;
	state->gui  = gui;
	state->init_caps.exceptions = NULL;
	state->first_letter.exceptions = NULL;

	if (dialog_init (state)) {
		go_gtk_notice_dialog (wbcg_toplevel (wbcg), GTK_MESSAGE_ERROR,
				 _("Could not create the AutoCorrect dialog."));
		cb_autocorrect_destroy (state);
		return;
	}

	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       AUTO_CORRECT_KEY);

	gtk_widget_show (state->dialog);
}

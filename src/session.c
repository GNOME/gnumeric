/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* session.c
 * Copyright (C) 2003-2006  Andreas J. Guelzow <aguelzow@taliesin.ca>
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
 */

#include <gnumeric-config.h>

#ifdef GNM_WITH_GNOME
#include "gnumeric.h"
#include <gtk/gtk.h>
#include <glib.h>
#include "session.h"
#include "application.h"
#include <workbook.h>
#include <workbook-priv.h>
#include <workbook-view.h>
#include <wbc-gtk.h>
#include <gui-file.h>
#include <gui-util.h>
#include <goffice/app/go-doc.h>
#include <glib/gi18n-lib.h>

#include <libgnomeui/gnome-client.h>

static GnomeClient *master_client = NULL;
static char const *program_argv0 = NULL;
static char const *current_dir = NULL;

static void
set_clone_restart (GnomeClient *client)
{
	GList *ptr, *workbooks;
	char **argv;
	int count = 1;

	argv = g_new0 (char *,
		       2 + g_list_length (gnm_app_workbook_list ()));

	argv[0] = (char *) program_argv0;

	workbooks = g_list_copy (gnm_app_workbook_list ());
	for (ptr = workbooks; ptr != NULL ; ptr = ptr->next) {
		Workbook *wb = ptr->data;
		if (wb->file_format_level == FILE_FL_AUTO) {
			argv[count] = g_strdup (go_doc_get_uri (GO_DOC (wb)));
			count++;
		}
	}

	gnome_client_set_clone_command (client, count, argv);
	gnome_client_set_restart_command (client, count, argv);

	g_free (argv);
}

static void
interaction_function (GnomeClient *client, gint key, GnomeDialogType dialog_type, gpointer shutdown)
{
	GList *ptr, *workbooks;
	gboolean ask_user = TRUE;
	gboolean do_not_cancel = FALSE;

	workbooks = g_list_copy (gnm_app_workbook_list ());
	for (ptr = workbooks; ptr != NULL ; ptr = ptr->next) {
		Workbook *wb = ptr->data;
		WBCGtk *wbcg = NULL;
		WorkbookView *wb_view = NULL;
		guint i;

		g_return_if_fail (IS_WORKBOOK (wb));

		if (wb->wb_views == NULL || wb->wb_views->len == 0)
			continue;
		wb_view = g_ptr_array_index (wb->wb_views, 0);
		if (wb_view == NULL)
			continue;
		for (i = 0; i < wb_view->wb_controls->len; i++)
			if (IS_WBC_GTK (g_ptr_array_index (wb_view->wb_controls, i)))
				wbcg = g_ptr_array_index (wb_view->wb_controls, i);
		if (wbcg == NULL)
			continue;

		wbcg_edit_finish (wbcg, WBC_EDIT_REJECT, NULL);

		if (!ask_user)
			if (!gui_file_save (wbcg, wb_view)) {
				do_not_cancel = TRUE;
				goto finished;
			}
		if (go_doc_is_dirty (GO_DOC (wb))) {
			GtkWidget *d;
			char *msg;
			int button = 0;
			char const *wb_uri = go_doc_get_uri (GO_DOC (wb));

			if (wb_uri) {
				char *base = g_path_get_basename (wb_uri);
				msg = g_strdup_printf (
					_("Save changes to workbook '%s' before logging out?"),
					base);
				g_free (base);
			} else
				msg = g_strdup (_("Save changes to workbook before logging out?"));
			gtk_window_deiconify (GTK_WINDOW (wbcg_toplevel (wbcg)));
			d = gnumeric_message_dialog_new (wbcg_toplevel (wbcg),
							 GTK_DIALOG_DESTROY_WITH_PARENT,
							 GTK_MESSAGE_WARNING,
							 msg,
							 _("If you do not save, changes may be discarded."));
			if (g_list_length (ptr) > 1) {
				go_gtk_dialog_add_button (GTK_DIALOG(d), _("Do not save any"),
							    GTK_STOCK_DELETE, - GTK_RESPONSE_NO);
				go_gtk_dialog_add_button (GTK_DIALOG(d), _("Do not save"),
							    GTK_STOCK_DELETE, GTK_RESPONSE_NO);
			} else
				go_gtk_dialog_add_button (GTK_DIALOG(d), _("Do not save"),
							    GTK_STOCK_DELETE, GTK_RESPONSE_NO);
			go_gtk_dialog_add_button (GTK_DIALOG(d), _("Do not log out"),
						    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
			gtk_dialog_add_button (GTK_DIALOG(d), GTK_STOCK_SAVE, GTK_RESPONSE_YES);
			gtk_dialog_set_default_response (GTK_DIALOG (d), GTK_RESPONSE_YES);

#warning We are making these windows sticky to work around a session manager bug that may make them inaccessible
			gtk_window_stick (GTK_WINDOW (wbcg_toplevel (wbcg)));
			gtk_window_stick (GTK_WINDOW (d));

			button = go_gtk_dialog_run (GTK_DIALOG (d), wbcg_toplevel (wbcg));

			g_free (msg);

			switch (button) {
			case GTK_RESPONSE_YES:
				if (!gui_file_save (wbcg, wb_view)) {
					gtk_window_unstick (GTK_WINDOW (wbcg_toplevel (wbcg)));
					do_not_cancel = TRUE;
					goto finished;
				}
				break;

			case (- GTK_RESPONSE_YES):
				if (!gui_file_save (wbcg, wb_view)) {
					gtk_window_unstick (GTK_WINDOW (wbcg_toplevel (wbcg)));
					do_not_cancel = TRUE;
					goto finished;
				}
				ask_user = FALSE;
				break;

			case GTK_RESPONSE_NO:
				break;

			case (- GTK_RESPONSE_NO):
				gtk_window_unstick (GTK_WINDOW (wbcg_toplevel (wbcg)));
				goto finished;
				break;

			default:  /* CANCEL */
				gtk_window_unstick (GTK_WINDOW (wbcg_toplevel (wbcg)));
				do_not_cancel = TRUE;
				goto finished;
				break;
			}
			gtk_window_unstick (GTK_WINDOW (wbcg_toplevel (wbcg)));

		}
	}
 finished:
	g_list_free (workbooks);

	set_clone_restart (client);

	gnome_interaction_key_return (key, do_not_cancel);

}

static gboolean
cb_client_save_yourself (GnomeClient *client, int phase,
			 GnomeSaveStyle what_to_save,
			 gboolean end, GnomeInteractStyle interaction,
			 gboolean fast)
{
	gboolean res = TRUE;

	if (!end)
		return TRUE;  /* If we aren't shutting down, don't bother us any further. */

	gnome_client_set_current_directory (client, current_dir);

	if (!(interaction == GNOME_INTERACT_ANY))
		res = FALSE;
	else
		gnome_client_request_interaction (client,
						  GNOME_DIALOG_NORMAL,
						  interaction_function,
						  NULL);
	set_clone_restart (client);
	return res;
}

static void
cb_client_die (GnomeClient *client)
{
	GList *ptr, *workbooks;

	workbooks = g_list_copy (gnm_app_workbook_list ());
	for (ptr = workbooks; ptr != NULL ; ptr = ptr->next) {
		go_doc_set_dirty (GO_DOC (ptr->data), FALSE);
		g_object_unref (ptr->data);
	}
	g_list_free (workbooks);
}

/**
 * gnm_session_init:
 *
 * Initializes session management support.  This function should be called near
 * the beginning of the program.
 **/
void
gnm_session_init (char const *argv0)
{
	if (master_client)
		return;

	program_argv0 = argv0;

	master_client = gnome_master_client ();

	current_dir = g_get_current_dir ();

	g_signal_connect (master_client, "save_yourself",
		G_CALLBACK (cb_client_save_yourself), NULL);
	g_signal_connect (master_client, "die",
		G_CALLBACK (cb_client_die), NULL);
}


#else
#include "session.h"
void
gnm_session_init (char const *argv0)
{
}
#endif /* GNM_WITH_GNOME */

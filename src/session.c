/* session.c
 * Copyright (C) 2003  Andreas J. Guelzow <aguelzow@taliesin.ca>
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
#include "gnumeric.h"
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <glib.h>
#include "session.h"
#include "application.h"
#include <gnumeric-i18n.h>
#include <workbook.h>
#include <workbook-priv.h>
#include <workbook-view.h>
#include <workbook-edit.h>
#include <workbook-control-gui.h>
#include <gui-file.h>
#include <gui-util.h>

#include <libgnomeui/gnome-client.h>

static GnomeClient *master_client = NULL;
static const char *program_argv0 = NULL;
static const char *current_dir = NULL;

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
			argv[count] = wb->filename;
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
		WorkbookControlGUI *wbcg = NULL;
		WorkbookView *wb_view = NULL;
		guint i;

		g_return_if_fail (IS_WORKBOOK (wb));

		if (wb->wb_views == NULL || wb->wb_views->len == 0)
			continue;
		wb_view = g_ptr_array_index (wb->wb_views, 0);
		if (wb_view == NULL)
			continue;
		for (i = 0; i < wb_view->wb_controls->len; i++)
			if (IS_WORKBOOK_CONTROL_GUI (g_ptr_array_index (wb_view->wb_controls, i)))
				wbcg = g_ptr_array_index (wb_view->wb_controls, i);
		if (wbcg == NULL)
			continue;
		
		wbcg_edit_finish (wbcg, FALSE, NULL);

		if (!ask_user)
			if (!gui_file_save (wbcg, wb_view)) {
				do_not_cancel = TRUE;
				goto finished;
			}
		if (workbook_is_dirty (wb)) {
			GtkWidget *d;
			char *msg;
			int button = 0;
			

			if (wb->filename) {
				char *base = g_path_get_basename (wb->filename);
				msg = g_strdup_printf (
					_("Save changes to workbook '%s' before logging out?"),
					base);
				g_free (base);
			} else
				msg = g_strdup (_("Save changes to workbook before logging out?"));
			d = gnumeric_message_dialog_new (wbcg_toplevel (wbcg),
							 GTK_DIALOG_DESTROY_WITH_PARENT,
							 GTK_MESSAGE_WARNING,
							 msg,
							 _("If you do not save, changes may be discarded."));
			if (g_list_length (ptr) > 1) {
			  	gnumeric_dialog_add_button (GTK_DIALOG(d), _("Do not save any"),
							    GTK_STOCK_DELETE, - GTK_RESPONSE_NO);
				gnumeric_dialog_add_button (GTK_DIALOG(d), _("Do not save"),
							    GTK_STOCK_DELETE, GTK_RESPONSE_NO);
			} else
				gnumeric_dialog_add_button (GTK_DIALOG(d), _("Do not save"),
							    GTK_STOCK_DELETE, GTK_RESPONSE_NO);
			gnumeric_dialog_add_button (GTK_DIALOG(d), _("Do not log out"), 
						    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
			gtk_dialog_add_button (GTK_DIALOG(d), GTK_STOCK_SAVE, GTK_RESPONSE_YES);
			gtk_dialog_set_default_response (GTK_DIALOG (d), GTK_RESPONSE_YES);

#warning We are making these windows sticky to work around a session manager bug that may make them inaccessible
			gtk_window_stick (GTK_WINDOW (wbcg_toplevel (wbcg)));
			gtk_window_stick (GTK_WINDOW (d));
			
			button = gnumeric_dialog_run (wbcg, GTK_DIALOG (d));

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
client_save_yourself_cb (GnomeClient *client, int phase, 
			 GnomeSaveStyle what_to_save,
			 gboolean end, GnomeInteractStyle interaction,
			 gboolean fast, gpointer data)
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
client_die_cb (GnomeClient *client, gpointer data)
{
	GList *ptr, *workbooks;

	workbooks = g_list_copy (gnm_app_workbook_list ());
	for (ptr = workbooks; ptr != NULL ; ptr = ptr->next) {
		Workbook *wb = ptr->data;

		g_return_if_fail (IS_WORKBOOK (wb));

		workbook_set_dirty (wb, FALSE);
		workbook_unref (wb);
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
gnm_session_init (const char *argv0)
{
	if (master_client)
		return;

	program_argv0 = argv0;
	
	master_client = gnome_master_client ();

	current_dir = g_get_current_dir ();

	g_signal_connect (master_client, "save_yourself",
			  G_CALLBACK (client_save_yourself_cb),
			  NULL);
	g_signal_connect (master_client, "die",
			  G_CALLBACK (client_die_cb),
			  NULL);
}



/*
 * dialog-recent.c:
 *   Dialog for selecting from recently used files.
 *
 * Author:
 *   Morten Welinder (terra@gnome.org)
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
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include "dialogs.h"
#include <wbc-gtk.h>
#include <gui-file.h>
#include <gtk/gtk.h>
#include <string.h>

/* ------------------------------------------------------------------------- */

static void
cb_response (GtkWidget *dialog,
	     gint response_id,
	     WBCGtk *wbcg)
{
	switch (response_id) {
	case GTK_RESPONSE_HELP:
		return;

	case GTK_RESPONSE_ACCEPT: {
		GtkRecentInfo *info =
			gtk_recent_chooser_get_current_item (GTK_RECENT_CHOOSER (dialog));
		const char *uri = info ? gtk_recent_info_get_uri (info) : NULL;
		if (uri)
			gui_file_read (wbcg, uri, NULL, NULL);

		/*
		 * This causes crashes with gtk+ 2.10.6, at least.
		 */
#if 0
		gtk_object_destroy (GTK_OBJECT (dialog));
#endif
		break;
	}

	default:
		gtk_object_destroy (GTK_OBJECT (dialog));
	}
}

void
dialog_recent_used (WBCGtk *wbcg)
{
	GtkRecentFilter *filter;
	GtkWidget *dialog = gtk_recent_chooser_dialog_new
		(_("Recently Used Files"),
		 wbcg_toplevel (wbcg),
		 GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		 GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
		 NULL);

	gtk_recent_chooser_set_sort_type (GTK_RECENT_CHOOSER (dialog), GTK_RECENT_SORT_MRU);

	filter = gtk_recent_filter_new ();
	gtk_recent_filter_set_name (filter, _("All files"));
	gtk_recent_filter_add_pattern (filter, "*");
	gtk_recent_chooser_add_filter (GTK_RECENT_CHOOSER (dialog), filter);

	filter = gtk_recent_filter_new ();
	gtk_recent_filter_set_name (filter, _("All files used by Gnumeric"));
	gtk_recent_filter_add_application (filter, g_get_application_name ());
	gtk_recent_chooser_add_filter (GTK_RECENT_CHOOSER (dialog), filter);
	gtk_recent_chooser_set_filter (GTK_RECENT_CHOOSER (dialog), filter);

	g_signal_connect (G_OBJECT (dialog), "response",
			  G_CALLBACK (cb_response), wbcg);

	go_gtk_nonmodal_dialog (wbcg_toplevel (wbcg), GTK_WINDOW (dialog));
	gtk_widget_show_all (GTK_WIDGET (dialog));
}

/* ------------------------------------------------------------------------- */

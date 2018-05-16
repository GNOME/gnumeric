/*
 * dialog-sheet-rename.c: Dialog to rename current sheet.
 *
 * Author:
 *	Morten Welinder <terra@gnome.org>
 *
 * (C) Copyright 2013 Morten Welinder <terra@gnome.org>
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <dialogs/dialogs.h>
#include <dialogs/help.h>

#include <gui-util.h>
#include <wbc-gtk.h>
#include <workbook-view.h>
#include <workbook.h>
#include <sheet.h>
#include <commands.h>

#define RENAME_DIALOG_KEY "sheet-rename-dialog"

typedef struct {
	WBCGtk *wbcg;
	Sheet *sheet;
	GtkWidget *dialog;
	GtkWidget *old_name, *new_name;
	GtkWidget *ok_button, *cancel_button;
	gint signal_connect_id_cb_dialog_size_allocate;
} RenameState;

static void
cb_name_changed (GtkEntry *e, RenameState *state)
{
	const gchar *name = gtk_entry_get_text (e);
	Sheet *sheet2 = workbook_sheet_by_name (state->sheet->workbook, name);
	gboolean valid;

	valid = (*name != 0) && (sheet2 == NULL || sheet2 == state->sheet);

	gtk_widget_set_sensitive (state->ok_button, valid);
}

static void
cb_ok_clicked (RenameState *state)
{
	const gchar *name = gtk_entry_get_text (GTK_ENTRY (state->new_name));

	if (! cmd_rename_sheet (GNM_WBC (state->wbcg),
			  state->sheet,
			  name))
		gtk_widget_destroy (state->dialog);
}

static void
gtk_entry_set_size_all_text_visible (GtkEntry *entry)
{
	PangoContext *context;
	PangoFontMetrics *metrics;
	gint char_width;
	gint digit_width;
	gint char_pixels;
	PangoLayout *pango_layout;
	gint char_count;
	gint min_width;
	gint actual_width;

	/* Logic borrowed from GtkEntry::gtk_entry_measure() */
	context = gtk_widget_get_pango_context (GTK_WIDGET (entry));
	metrics = pango_context_get_metrics (context,
					     pango_context_get_font_description (context),
					     pango_context_get_language (context));

	char_width = pango_font_metrics_get_approximate_char_width (metrics);
	digit_width = pango_font_metrics_get_approximate_digit_width (metrics);
	char_pixels = (MAX (char_width, digit_width) + PANGO_SCALE - 1) / PANGO_SCALE;

	pango_layout = gtk_entry_get_layout (entry);
	char_count = pango_layout_get_character_count (pango_layout);
	min_width = char_pixels * char_count;
	actual_width = gtk_widget_get_allocated_width (GTK_WIDGET (entry));

	if (actual_width < min_width)
		gtk_entry_set_width_chars (entry, char_count);
}

static void
cb_dialog_size_allocate (GtkWidget *dialog, GdkRectangle *allocation, RenameState *state)
{
	GdkGeometry hints;

	g_signal_handler_disconnect (G_OBJECT (dialog),
				     state->signal_connect_id_cb_dialog_size_allocate);

	/* dummy values for min/max_width to not restrict horizontal resizing */
	hints.min_width = 0;
	hints.max_width = G_MAXINT;
	/* do not allow vertial resizing */
	hints.min_height = allocation->height;
	hints.max_height = allocation->height;
	gtk_window_set_geometry_hints (GTK_WINDOW (dialog), (GtkWidget *) NULL,
				       &hints,
				       (GdkWindowHints) (GDK_HINT_MIN_SIZE | GDK_HINT_MAX_SIZE));

	gtk_entry_set_size_all_text_visible (GTK_ENTRY (state->new_name));
}

void
dialog_sheet_rename (WBCGtk *wbcg, Sheet *sheet)
{
	GtkBuilder *gui;
	RenameState *state;

	if (gnm_dialog_raise_if_exists (wbcg, RENAME_DIALOG_KEY))
		return;
	gui = gnm_gtk_builder_load ("res:ui/sheet-rename.ui", NULL, GO_CMD_CONTEXT (wbcg));
	if (gui == NULL)
		return;

	state = g_new (RenameState, 1);
	state->wbcg = wbcg;
	state->dialog = go_gtk_builder_get_widget (gui, "Rename");
	state->sheet = sheet;
	g_return_if_fail (state->dialog != NULL);

	state->signal_connect_id_cb_dialog_size_allocate =
		g_signal_connect (G_OBJECT (state->dialog),
				  "size-allocate",
				  G_CALLBACK (cb_dialog_size_allocate),
				  state);

	state->old_name = go_gtk_builder_get_widget (gui, "old_name");
	gtk_entry_set_text (GTK_ENTRY (state->old_name), sheet->name_unquoted);

	state->new_name = go_gtk_builder_get_widget (gui, "new_name");
	gtk_entry_set_text (GTK_ENTRY (state->new_name), sheet->name_unquoted);

	gtk_editable_select_region (GTK_EDITABLE (state->new_name), 0, -1);
	gtk_widget_grab_focus (state->new_name);
	g_signal_connect (G_OBJECT (state->new_name),
			  "changed", G_CALLBACK (cb_name_changed),
			  state);
	gnm_editable_enters (GTK_WINDOW (state->dialog), state->new_name);

	state->ok_button = go_gtk_builder_get_widget (gui, "ok_button");
	g_signal_connect_swapped (G_OBJECT (state->ok_button),
				  "clicked", G_CALLBACK (cb_ok_clicked),
				  state);

	state->cancel_button = go_gtk_builder_get_widget (gui, "cancel_button");
	g_signal_connect_swapped (G_OBJECT (state->cancel_button),
				  "clicked", G_CALLBACK (gtk_widget_destroy),
				  state->dialog);

	gnm_dialog_setup_destroy_handlers (GTK_DIALOG (state->dialog), wbcg,
					   GNM_DIALOG_DESTROY_SHEET_REMOVED);

	gnm_keyed_dialog (wbcg, GTK_WINDOW (state->dialog),
			       RENAME_DIALOG_KEY);

	g_object_set_data_full (G_OBJECT (state->dialog),
				"state", state,
				(GDestroyNotify) g_free);
	g_object_unref (gui);

	gtk_widget_show (state->dialog);
}

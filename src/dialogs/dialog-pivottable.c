/**
 * dialog-pivottable.c:  Edit PivotTables
 *
 * (c) Copyright 2002 Jody Goldberg <jody@gnome.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
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
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include "dialogs.h"

#include <gui-util.h>
#include <commands.h>
#include <workbook-control.h>
#include <workbook.h>
#include <workbook-edit.h>
#include <sheet.h>
#include <sheet-view.h>
#include <workbook-cmd-format.h>
#include <pivottable.h>

#include <glade/glade.h>

typedef struct {
	GladeXML           *gui;
	WorkbookControlGUI *wbcg;
	Sheet              *sheet;
	SheetView	   *sv;
	GtkWidget          *dialog;
} PivotTableGuru;

#define GLADE_FILE "pivottable.glade"
#define DIALOG_KEY "pivottable-guru"

static void
cb_pivottable_guru_destroy (PivotTableGuru *state)
{
	wbcg_edit_detach_guru (state->wbcg);

	if (state->gui != NULL) {
		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}

	state->dialog = NULL;
	g_free (state);
}

static void
cb_pivottable_guru_ok (G_GNUC_UNUSED GtkWidget *button,
		       PivotTableGuru *state)
{
	gtk_widget_destroy (state->dialog);
}

static void
cb_pivottable_guru_cancel (G_GNUC_UNUSED GtkWidget *button,
			   PivotTableGuru *state)
{
	gtk_widget_destroy (state->dialog);
}

void
dialog_pivottable (WorkbookControlGUI *wbcg)
{
	PivotTableGuru *state;
	GtkWidget *w;

	g_return_if_fail (wbcg != NULL);

	if (gnumeric_dialog_raise_if_exists (wbcg, DIALOG_KEY))
		return;

	state = g_new (PivotTableGuru, 1);
	state->wbcg  = wbcg;
	state->sv    = wb_control_cur_sheet_view (WORKBOOK_CONTROL (wbcg));
	state->sheet = sv_sheet (state->sv);
	state->gui = gnumeric_glade_xml_new (wbcg, GLADE_FILE);
	g_return_if_fail (state->gui != NULL);

	state->dialog = glade_xml_get_widget (state->gui, "pivottable_guru");

	w = glade_xml_get_widget (state->gui, "ok_button");
	g_signal_connect (G_OBJECT (w),
		"clicked",
		G_CALLBACK (cb_pivottable_guru_ok), state);
	w = glade_xml_get_widget (state->gui, "cancel_button");
	g_signal_connect (G_OBJECT (w),
		"clicked",
		G_CALLBACK (cb_pivottable_guru_cancel), state);

	/* a candidate for merging into attach guru */
	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "help_button"),
		"pivottable.html");
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify)cb_pivottable_guru_destroy);
	wbcg_edit_attach_guru (state->wbcg, state->dialog);
	gnumeric_keyed_dialog (wbcg, GTK_WINDOW (state->dialog), DIALOG_KEY);
	gtk_widget_show (state->dialog);
}

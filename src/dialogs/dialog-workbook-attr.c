/**
 * dialog-workbook-attr.c:  Implements a dialog to set workbook attributes.
 *
 * Author:
 *  JP Rosevear <jpr@arcavia.com>
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
#include "help.h"

#include <gui-util.h>
#include <workbook-view.h>
#include <workbook.h>
#include <wbc-gtk.h>
#include <workbook-priv.h>
#include <gtk/gtktogglebutton.h>
#include <gtk/gtknotebook.h>

#define WORKBOOK_ATTRIBUTE_KEY "workbook-attribute-dialog"

typedef struct {
	GladeXML	*gui;
	GtkWidget	*dialog;
	GtkWidget	*notebook;
	GtkWidget	*ok_button;
	GtkWidget	*apply_button;
	GtkWidget	*iteration_table;
	gboolean         destroying;

	Workbook	 *wb;
	WorkbookView     *wbv;
	WBCGtk	 *wbcg;

	struct {
		GtkToggleButton	*show_hsb;
		GtkToggleButton	*show_vsb;
		GtkToggleButton	*show_tabs;
		GtkToggleButton	*autocomplete;
		GtkToggleButton	*is_protected;
		GtkToggleButton	*recalc_auto;
		GtkToggleButton *iteration_enabled;
		GtkEntry	*max_iterations;
		GtkEntry	*iteration_tolerance;
	} view;
	struct {
		gboolean	show_hsb;
		gboolean	show_vsb;
		gboolean	show_tabs;
		gboolean	autocomplete;
		gboolean	is_protected;
		gboolean	recalc_auto;
		gboolean	iteration_enabled;
		int		max_iterations;
		gnm_float	iteration_tolerance;
	} old;
} AttrState;

/*****************************************************************************/
/* Some utility routines shared by all pages */

/* Default to the 'View' page but remember which page we were on between
 * invocations */
static int attr_dialog_page = 0;

/*
 * Callback routine to help remember which attribute tab was selected
 * between dialog invocations.
 */
static void
cb_page_select (G_GNUC_UNUSED GtkNotebook *notebook,
		G_GNUC_UNUSED GtkNotebookPage *page,
		gint page_num,  AttrState *state)
{
	if (!state->destroying)
		attr_dialog_page = page_num;
}

/*****************************************************************************/

static void
get_entry_values (AttrState *state, int *max_iterations, gnm_float *iteration_tolerance)
{
	if (!entry_to_int (state->view.max_iterations, max_iterations, TRUE))
		*max_iterations = state->old.max_iterations;
	if (!entry_to_float (state->view.iteration_tolerance, iteration_tolerance, TRUE))
		*iteration_tolerance = state->old.iteration_tolerance;
}

static void
cb_widget_changed (G_GNUC_UNUSED GtkWidget *widget, AttrState *state)
{
	gboolean changed;
	int max_iterations;
	gnm_float iteration_tolerance;

	get_entry_values (state, &max_iterations, &iteration_tolerance);
	changed =
		!((gtk_toggle_button_get_active (state->view.show_hsb) == state->old.show_hsb) &&
		  (gtk_toggle_button_get_active (state->view.show_vsb) == state->old.show_vsb) &&
		  (gtk_toggle_button_get_active (state->view.show_tabs) == state->old.show_tabs) &&
		  (gtk_toggle_button_get_active (state->view.autocomplete) == state->old.autocomplete) &&
		  (gtk_toggle_button_get_active (state->view.is_protected) == state->old.is_protected) &&
		  (gtk_toggle_button_get_active (state->view.recalc_auto) == state->old.recalc_auto) &&
		  (gtk_toggle_button_get_active (state->view.iteration_enabled) == state->old.iteration_enabled) &&
		  (max_iterations == state->old.max_iterations) &&
		  (iteration_tolerance == state->old.iteration_tolerance));

	gtk_widget_set_sensitive (state->ok_button, changed);
	gtk_widget_set_sensitive (state->apply_button, changed);

	gtk_widget_set_sensitive (state->iteration_table,
		gtk_toggle_button_get_active (state->view.iteration_enabled));
}

/* Handler for the apply button */
static void
cb_attr_dialog_dialog_apply (G_GNUC_UNUSED GtkWidget *button,
			     AttrState *state)
{
	state->wbv->show_horizontal_scrollbar = state->old.show_hsb =
		gtk_toggle_button_get_active (state->view.show_hsb);
	state->wbv->show_vertical_scrollbar = state->old.show_vsb =
		gtk_toggle_button_get_active (state->view.show_vsb);
	state->wbv->show_notebook_tabs = state->old.show_tabs =
		gtk_toggle_button_get_active (state->view.show_tabs);
	state->wbv->do_auto_completion = state->old.autocomplete =
		gtk_toggle_button_get_active (state->view.autocomplete);
	state->wbv->is_protected = state->old.is_protected =
		gtk_toggle_button_get_active (state->view.is_protected);

	state->old.recalc_auto =
		gtk_toggle_button_get_active (state->view.recalc_auto);
	state->old.iteration_enabled =
		gtk_toggle_button_get_active (state->view.iteration_enabled);

	get_entry_values (state, &state->old.max_iterations,
			  &state->old.iteration_tolerance);
	workbook_set_recalcmode	(state->wb, state->old.recalc_auto);
	workbook_iteration_enabled	(state->wb, state->old.iteration_enabled);
	workbook_iteration_max_number	(state->wb, state->old.max_iterations);
	workbook_iteration_tolerance	(state->wb, state->old.iteration_tolerance);

	cb_widget_changed (NULL, state);
}

static void
cb_attr_dialog_dialog_close (G_GNUC_UNUSED GtkWidget *button,
			     AttrState *state)
{
	state->destroying = TRUE;
	gtk_widget_destroy (state->dialog);
}

static void
cb_attr_dialog_dialog_ok (GtkWidget *button, AttrState *state)
{
	cb_attr_dialog_dialog_apply (button, state);
	cb_attr_dialog_dialog_close (button, state);
}

static void
cb_attr_dialog_dialog_destroy (AttrState *state)
{
	if (state->gui != NULL) {
		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}

	state->dialog = NULL;
	g_free (state);
}

/*****************************************************************************/

static GtkToggleButton *
attr_dialog_init_toggle (AttrState *state, char const *name, gboolean val,
			 gboolean *storage)
{
	GtkWidget *w = glade_xml_get_widget (state->gui, name);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), val);
	g_signal_connect (G_OBJECT (w),
		"toggled",
		G_CALLBACK (cb_widget_changed), state);
	*storage = val;

	return GTK_TOGGLE_BUTTON (w);
}

static GtkEntry *
attr_dialog_init_entry (AttrState *state, char const *name, char const *val)
{
	GtkWidget *w = glade_xml_get_widget (state->gui, name);
	gtk_entry_set_text (GTK_ENTRY (w), val);
	g_signal_connect (G_OBJECT (w),
		"changed",
		G_CALLBACK (cb_widget_changed), state);
	gnumeric_editable_enters (GTK_WINDOW (state->dialog), w);
	return GTK_ENTRY (w);
}

static void
attr_dialog_init_view_page (AttrState *state)
{
	char *buf;

	state->view.show_hsb     = attr_dialog_init_toggle (state,
		"WorkbookView::show_horizontal_scrollbar",
		state->wbv->show_horizontal_scrollbar,
		&state->old.show_hsb);
	state->view.show_vsb     = attr_dialog_init_toggle (state,
		"WorkbookView::show_vertical_scrollbar",
		state->wbv->show_vertical_scrollbar,
		&state->old.show_vsb);
	state->view.show_tabs    = attr_dialog_init_toggle (state,
		"WorkbookView::show_notebook_tabs",
		state->wbv->show_notebook_tabs,
		&state->old.show_tabs);
	state->view.autocomplete = attr_dialog_init_toggle (state,
		"WorkbookView::do_auto_completion",
		state->wbv->do_auto_completion,
		&state->old.autocomplete);
	state->view.is_protected = attr_dialog_init_toggle (state,
		"WorkbookView::workbook_protected",
		state->wbv->is_protected,
		&state->old.is_protected);
	if (!workbook_get_recalcmode (state->wb)) {
		GtkWidget *w = glade_xml_get_widget (state->gui, "recalc_manual");
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), TRUE);
	}
	state->view.recalc_auto = attr_dialog_init_toggle (state,
		"recalc_auto",
		workbook_get_recalcmode (state->wb),
		&state->old.recalc_auto);
	state->view.iteration_enabled = attr_dialog_init_toggle (state,
		"iteration_enabled",
		state->wb->iteration.enabled,
		&state->old.iteration_enabled);

	buf = g_strdup_printf ("%d", state->wb->iteration.max_number);
	state->old.max_iterations = state->wb->iteration.max_number;
	state->view.max_iterations =
		attr_dialog_init_entry (state, "max_iterations", buf);
	g_free (buf);

	buf = g_strdup_printf ("%g", state->wb->iteration.tolerance);
	state->old.iteration_tolerance = state->wb->iteration.tolerance;
	state->view.iteration_tolerance =
		attr_dialog_init_entry (state, "iteration_tolerance", buf);
	g_free (buf);
}

/*****************************************************************************/

static void
attr_dialog_impl (AttrState *state)
{
	GtkWidget *dialog = glade_xml_get_widget (state->gui, "WorkbookAttr");
	g_return_if_fail (dialog != NULL);

	/* Initialize */
	state->dialog			= dialog;
	state->notebook                 = glade_xml_get_widget (state->gui, "notebook");
	state->destroying               = FALSE;

	attr_dialog_init_view_page (state);

	/* Select the same page the last invocation used */
	gtk_notebook_set_current_page (
		GTK_NOTEBOOK (state->notebook),
		attr_dialog_page);
        g_signal_connect (
		G_OBJECT (state->notebook),
		"switch_page",
		G_CALLBACK (cb_page_select), state);

	state->iteration_table = glade_xml_get_widget (state->gui, "iteration_table");
	state->ok_button = glade_xml_get_widget (state->gui, "ok_button");
	g_signal_connect (G_OBJECT (state->ok_button),
			  "clicked",
			  G_CALLBACK (cb_attr_dialog_dialog_ok), state);
	state->apply_button = glade_xml_get_widget (state->gui, "apply_button");
	g_signal_connect (G_OBJECT (state->apply_button),
			  "clicked",
			  G_CALLBACK (cb_attr_dialog_dialog_apply), state);
	g_signal_connect (G_OBJECT (glade_xml_get_widget (state->gui, "close_button")),
			  "clicked",
			  G_CALLBACK (cb_attr_dialog_dialog_close), state);
	cb_widget_changed (NULL, state);

	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "help_button"),
		GNUMERIC_HELP_LINK_WORKBOOK_ATTRIBUTE);

	/* a candidate for merging into attach guru */
	g_object_set_data_full (G_OBJECT (dialog),
		"state", state, (GDestroyNotify) cb_attr_dialog_dialog_destroy);
	wbc_gtk_attach_guru (state->wbcg, state->dialog);
	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       WORKBOOK_ATTRIBUTE_KEY);
	gtk_widget_show (state->dialog);
}

void
dialog_workbook_attr (WBCGtk *wbcg)
{
	GladeXML     *gui;
	AttrState    *state;

	g_return_if_fail (wbcg != NULL);

	if (gnumeric_dialog_raise_if_exists (wbcg, WORKBOOK_ATTRIBUTE_KEY))
		return;

	gui = gnm_glade_xml_new (GO_CMD_CONTEXT (wbcg),
		"workbook-attr.glade", NULL, NULL);
        if (gui == NULL)
                return;

	/* Initialize */
	state = g_new (AttrState, 1);
	state->gui = gui;
	state->wbcg = wbcg;
	state->wbv  = wb_control_view (WORKBOOK_CONTROL (wbcg));
	state->wb   = wb_control_get_workbook (WORKBOOK_CONTROL (wbcg));

	attr_dialog_impl (state);
}

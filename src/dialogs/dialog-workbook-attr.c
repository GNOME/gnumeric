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
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include "dialogs.h"

#include <sheet.h>
#include <style-color.h>
#include <widgets/widget-font-selector.h>
#include <widgets/gnumeric-dashed-canvas-line.h>
#include <gui-util.h>
#include <selection.h>
#include <ranges.h>
#include <format.h>
#include <formats.h>
#include <pattern.h>
#include <mstyle.h>
#include <application.h>
#include <workbook-control.h>
#include <workbook-view.h>
#include <workbook-edit.h>
#include <workbook.h>
#include <commands.h>

#include <glade/glade.h>

#define WORKBOOK_ATTRIBUTE_KEY "workbook-attribute-dialog"

typedef struct _AttrState
{
	GladeXML	 *gui;
	GtkWidget        *dialog;
	GtkWidget        *notebook;
	GtkWidget        *ok_button;
	GtkWidget        *apply_button;
	gboolean         destroying;

	WorkbookView     *wbv;
	WorkbookControlGUI	 *wbcg;

	struct {
		GtkToggleButton	*show_hsb;
		GtkToggleButton	*show_vsb;
		GtkToggleButton	*show_tabs;
		GtkToggleButton	*autocomplete;
		GtkToggleButton	*is_protected;
	} view;
	struct {
		gboolean	show_hsb;
		gboolean	show_vsb;
		gboolean	show_tabs;
		gboolean	autocomplete;
		gboolean	is_protected;
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
cb_page_select (GtkNotebook *notebook, GtkNotebookPage *page,
		gint page_num,  AttrState *state)
{
	if (!state->destroying)
		attr_dialog_page = page_num;
}

/*****************************************************************************/

/* Handler for the apply button */
static void
cb_attr_dialog_dialog_apply (GtkWidget *button, AttrState *state)
{
	state->wbv->show_horizontal_scrollbar =
		gtk_toggle_button_get_active (state->view.show_hsb);
	state->wbv->show_vertical_scrollbar =
		gtk_toggle_button_get_active (state->view.show_vsb);
	state->wbv->show_notebook_tabs =
		gtk_toggle_button_get_active (state->view.show_tabs);
	state->wbv->do_auto_completion =
		gtk_toggle_button_get_active (state->view.autocomplete);
	state->wbv->is_protected =
		gtk_toggle_button_get_active (state->view.is_protected);

	wb_view_prefs_update (state->wbv);
}

static void
cb_attr_dialog_dialog_close (GtkWidget *button, AttrState *state)
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

/* Handler for destroy */
static gboolean
cb_attr_dialog_dialog_destroy (GtkObject *w, AttrState *state)
{
	g_return_val_if_fail (w != NULL, FALSE);
	g_return_val_if_fail (state != NULL, FALSE);

	wbcg_edit_detach_guru (state->wbcg);

	if (state->gui != NULL) {
		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}

	state->dialog = NULL;
	g_free (state);

	return FALSE;
}

/*****************************************************************************/

static void
cb_toggled (GtkWidget *widget, AttrState *state)
{
	gboolean changed = !((gtk_toggle_button_get_active (state->view.show_hsb) 
			    == state->old.show_hsb) &&
		(gtk_toggle_button_get_active (state->view.show_vsb) 
		 == state->old.show_vsb) &&
		(gtk_toggle_button_get_active (state->view.show_tabs) 
		 == state->old.show_tabs) &&
		(gtk_toggle_button_get_active (state->view.autocomplete) 
					       == state->old.autocomplete) &&
		(gtk_toggle_button_get_active (state->view.is_protected) 
		 == state->old.is_protected));
	gtk_widget_set_sensitive (state->ok_button, changed);
	gtk_widget_set_sensitive (state->apply_button, changed);
}

static GtkToggleButton *
attr_dialog_init_toggle (AttrState *state, char const *name, gboolean val,
			 gboolean *storage)
{
	GtkWidget *w = glade_xml_get_widget (state->gui, name);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), val);
	g_signal_connect (G_OBJECT (w),
		"toggled",
		G_CALLBACK (cb_toggled), state);
	*storage = val;

	return GTK_TOGGLE_BUTTON (w);
}

static void
attr_dialog_init_view_page (AttrState *state)
{
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
	gtk_notebook_set_page (
		GTK_NOTEBOOK (state->notebook),
		attr_dialog_page);
        g_signal_connect (
		G_OBJECT (state->notebook),
		"switch_page",
		G_CALLBACK (cb_page_select), state);


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
/* FIXME: Add correct helpfile address */
	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "help_button"),
		"workbook-attributes.html");

	g_signal_connect (G_OBJECT (dialog),
		"destroy",
		G_CALLBACK (cb_attr_dialog_dialog_destroy), state);

	cb_toggled (NULL, state);

	wbcg_edit_attach_guru (state->wbcg, state->dialog);
	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       WORKBOOK_ATTRIBUTE_KEY);
	gtk_widget_show (state->dialog);
}

void
dialog_workbook_attr (WorkbookControlGUI *wbcg)
{
	GladeXML     *gui;
	AttrState    *state;

	g_return_if_fail (wbcg != NULL);

	if (gnumeric_dialog_raise_if_exists (wbcg, WORKBOOK_ATTRIBUTE_KEY))
		return;

	gui = gnumeric_glade_xml_new (wbcg, "workbook-attr.glade");
        if (gui == NULL)
                return;

	/* Initialize */
	state = g_new (AttrState, 1);
	state->gui = gui;
	state->wbcg = wbcg;
	state->wbv  = wb_control_view (WORKBOOK_CONTROL (wbcg));

	attr_dialog_impl (state);
}

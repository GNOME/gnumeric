/**
 * dialog-workbook-attr.c:  Implements a dialog to set workbook attributes.
 *
 * Author:
 *  JP Rosevear <jpr@arcavia.com>
 *
 **/

#include <gnumeric-config.h>
#include <gnumeric.h>
#include "dialogs.h"

#include <sheet.h>
#include <style-color.h>
#include <utils-dialog.h>
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
#include <workbook.h>
#include <commands.h>

#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-help.h>
#include <glade/glade.h>

typedef struct _AttrState
{
	GladeXML	 *gui;
	GnomePropertyBox *dialog;
	gint		  page_signal;

	WorkbookView     *wbv;
	WorkbookControlGUI	 *wbcg;

	gboolean	  enable_edit;

	struct {
		GtkToggleButton	*show_hsb;
		GtkToggleButton	*show_vsb;
		GtkToggleButton	*show_tabs;
		GtkToggleButton	*autocomplete;
	} view;
} AttrState;

/*****************************************************************************/
/* Some utility routines shared by all pages */

/*
 * A utility routine to help mark the attributes as being changed
 * VERY stupid for now.
 */
static void
attr_dialog_changed (AttrState *state)
{
	/* Catch all the pseudo-events that take place while initializing */
	if (state->enable_edit)
		gnome_property_box_changed (state->dialog);
}

/* Default to the 'View' page but remember which page we were on between
 * invocations */
static int attr_dialog_page = 0;

/*
 * Callback routine to help remember which attribute tab was selected
 * between dialog invocations.
 */
static void
cb_page_select (GtkNotebook *notebook, GtkNotebookPage *page,
		gint page_num, gpointer user_data)
{
	attr_dialog_page = page_num;
}

/*****************************************************************************/

/* Handler for the apply button */
static void
cb_attr_dialog_dialog_apply (GtkObject *w, int page, AttrState *state)
{
	state->wbv->show_horizontal_scrollbar =
		gtk_toggle_button_get_active (state->view.show_hsb);
	state->wbv->show_vertical_scrollbar =
		gtk_toggle_button_get_active (state->view.show_vsb);
	state->wbv->show_notebook_tabs =
		gtk_toggle_button_get_active (state->view.show_tabs);
	state->wbv->do_auto_completion =
		gtk_toggle_button_get_active (state->view.autocomplete);

	wb_view_prefs_update (state->wbv);
}

/* Handler for destroy */
static gboolean
cb_attr_dialog_dialog_destroy (GtkObject *w, AttrState *state)
{
	GnomePropertyBox *box = state->dialog;

	gtk_signal_disconnect (GTK_OBJECT (box->notebook),
			       state->page_signal);
	gtk_object_unref (GTK_OBJECT (state->gui));
	g_free (state);

	return FALSE;
}

/*****************************************************************************/

static void
cb_toggled (GtkWidget *widget, AttrState *state)
{
	attr_dialog_changed (state);
}

static GtkToggleButton *
attr_dialog_init_toggle (AttrState *state, char const *name, gboolean val)
{
	GtkWidget *w = glade_xml_get_widget (state->gui, name);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), val);
	gtk_signal_connect (GTK_OBJECT (w),
		"toggled",
		GTK_SIGNAL_FUNC (cb_toggled), state);

	return GTK_TOGGLE_BUTTON (w);
}

static void
attr_dialog_init_view_page (AttrState *state)
{
	state->view.show_hsb     = attr_dialog_init_toggle (state,
		"WorkbookView::show_horizontal_scrollbar",
		state->wbv->show_horizontal_scrollbar);
	state->view.show_vsb     = attr_dialog_init_toggle (state,
		"WorkbookView::show_vertical_scrollbar",
		state->wbv->show_vertical_scrollbar);
	state->view.show_tabs    = attr_dialog_init_toggle (state,
		"WorkbookView::show_notebook_tabs",
		state->wbv->show_notebook_tabs);
	state->view.autocomplete = attr_dialog_init_toggle (state,
		"WorkbookView::do_auto_completion",
		state->wbv->do_auto_completion);
}

/*****************************************************************************/

/*
 * NOTE: We have to set the dialog title here. Looks like <title> in glade
 * file doesn't work for property dialogs.
 */
static void
attr_dialog_impl (AttrState *state)
{
	static GnomeHelpMenuEntry help_ref = { "gnumeric", "" };

	GtkWidget *dialog = glade_xml_get_widget (state->gui, "WorkbookAttr");
	g_return_if_fail (dialog != NULL);

	gtk_window_set_title (GTK_WINDOW (dialog), _("Workbook Attributes"));

	/* Initialize */
	state->dialog			= GNOME_PROPERTY_BOX (dialog);

	state->enable_edit		= FALSE;  /* Enable below */

	attr_dialog_init_view_page (state);

	/* Select the same page the last invocation used */
	gtk_notebook_set_page (
		GTK_NOTEBOOK (GNOME_PROPERTY_BOX (dialog)->notebook),
		attr_dialog_page);
	state->page_signal = gtk_signal_connect (
		GTK_OBJECT (GNOME_PROPERTY_BOX (dialog)->notebook),
		"switch_page", GTK_SIGNAL_FUNC (cb_page_select),
		NULL);

	/* Setup help */
	gtk_signal_connect (GTK_OBJECT (dialog), "help",
			    GTK_SIGNAL_FUNC (gnome_help_pbox_goto), &help_ref);

	/* Handle apply */
	gtk_signal_connect (GTK_OBJECT (dialog), "apply",
			    GTK_SIGNAL_FUNC (cb_attr_dialog_dialog_apply),
			    state);

	/* Handle destroy */
	gtk_signal_connect (GTK_OBJECT (dialog), "destroy",
			    GTK_SIGNAL_FUNC (cb_attr_dialog_dialog_destroy),
			    state);

	/* Ok, edit events from now on are real */
	state->enable_edit = TRUE;

	/* Make it modal */
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);

	/* Bring up the dialog */
	gnumeric_dialog_show (state->wbcg, GNOME_DIALOG (dialog), FALSE, TRUE);
}

void
dialog_workbook_attr (WorkbookControlGUI *wbcg)
{
	GladeXML     *gui;
	AttrState    *state;

	g_return_if_fail (wbcg != NULL);

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

/**
 * dialog-workbook-attr.c:  Implements a dialog to set workbook attributes.
 *
 * Author:
 *  JP Rosevear <jpr@arcavia.com>
 *
 **/

#include <config.h>
#include <gnome.h>
#include <glade/glade.h>
#include "sheet.h"
#include "color.h"
#include "dialogs.h"
#include "utils-dialog.h"
#include "widgets/widget-font-selector.h"
#include "widgets/gnumeric-dashed-canvas-line.h"
#include "gnumeric-util.h"
#include "selection.h"
#include "ranges.h"
#include "format.h"
#include "formats.h"
#include "pattern.h"
#include "mstyle.h"
#include "application.h"
#include "workbook.h"
#include "workbook-view.h"
#include "commands.h"

#define GLADE_FILE "workbook-attr.glade"

typedef struct _AttrState
{
	GladeXML	*gui;
	GnomePropertyBox*dialog;
	gint		 page_signal;

	Workbook         *wb;

	gboolean	 enable_edit;

	struct
	{
		GtkToggleButton	*show_hsb;
		GtkToggleButton	*show_vsb;
		GtkToggleButton	*show_tabs;
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

static void
cb_notebook_destroy (GtkObject *obj, AttrState *state)
{
	gtk_signal_disconnect (obj, state->page_signal);
}

/*****************************************************************************/

/* Handler for the apply button */
static void
cb_attr_dialog_dialog_apply (GtkObject *w, int page, AttrState *state)
{
	state->wb->show_horizontal_scrollbar = gtk_toggle_button_get_active (state->view.show_hsb);
	state->wb->show_vertical_scrollbar = gtk_toggle_button_get_active (state->view.show_vsb);
	state->wb->show_notebook_tabs = gtk_toggle_button_get_active (state->view.show_tabs);
	
	workbook_view_pref_visibility (state->wb);
}

/* Handler for destroy */
static gboolean
cb_attr_dialog_dialog_destroy (GtkObject *w, AttrState *state)
{
	g_free (state);
	return FALSE;
}

/*****************************************************************************/

static void
cb_show_hsb_toggled (GtkWidget *widget, AttrState *state)
{
	attr_dialog_changed (state);
}

static void
cb_show_vsb_toggled (GtkWidget *widget, AttrState *state)
{
	attr_dialog_changed (state);
}

static void
cb_show_tabs_toggled (GtkWidget *widget, AttrState *state)
{
	attr_dialog_changed (state);
}

static void
attr_dialog_init_view_page (AttrState *state)
{
	state->view.show_hsb = GTK_TOGGLE_BUTTON (glade_xml_get_widget (state->gui, "Workbook::show_horizontal_scrollbar"));
	state->view.show_vsb = GTK_TOGGLE_BUTTON (glade_xml_get_widget (state->gui, "Workbook::show_vertical_scrollbar"));
	state->view.show_tabs = GTK_TOGGLE_BUTTON (glade_xml_get_widget (state->gui, "Workbook::show_notebook_tabs"));

	gtk_toggle_button_set_active (state->view.show_hsb, state->wb->show_horizontal_scrollbar);
	gtk_toggle_button_set_active (state->view.show_vsb, state->wb->show_vertical_scrollbar);
	gtk_toggle_button_set_active (state->view.show_tabs, state->wb->show_notebook_tabs);
	
	/* Setup special handlers for : Numbers */
	gtk_signal_connect (GTK_OBJECT (state->view.show_hsb),
			    "toggled",
			    GTK_SIGNAL_FUNC (cb_show_hsb_toggled),
			    state);

	gtk_signal_connect (GTK_OBJECT (state->view.show_vsb),
			    "toggled",
			    GTK_SIGNAL_FUNC (cb_show_vsb_toggled),
			    state);
	
	gtk_signal_connect (GTK_OBJECT (state->view.show_tabs),
			    "toggled",
			    GTK_SIGNAL_FUNC (cb_show_tabs_toggled),
			    state);
}


/*****************************************************************************/

static void
attr_dialog_impl (AttrState *state)
{
	static GnomeHelpMenuEntry help_ref = { "gnumeric", "" };
	
	GtkWidget *dialog = glade_xml_get_widget (state->gui, "WorkbookAttr");
	g_return_if_fail (dialog != NULL);

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
	gtk_signal_connect (
		GTK_OBJECT (GNOME_PROPERTY_BOX (dialog)->notebook),
		"destroy", GTK_SIGNAL_FUNC (cb_notebook_destroy),
		state);

	/* Setup help */
	gtk_signal_connect (GTK_OBJECT (dialog), "help",
			    GTK_SIGNAL_FUNC (gnome_help_pbox_goto), &help_ref);

	/* Handle apply */
	gtk_signal_connect (GTK_OBJECT (dialog), "apply",
			    GTK_SIGNAL_FUNC (cb_attr_dialog_dialog_apply),
			    state);

	/* Handle destroy */
	gtk_signal_connect(GTK_OBJECT(dialog), "destroy",
			   GTK_SIGNAL_FUNC(cb_attr_dialog_dialog_destroy),
			   state);

	/* Ok, edit events from now on are real */
	state->enable_edit = TRUE;

	/* Make it modal */
	gtk_window_set_modal (GTK_WINDOW(dialog), TRUE);
	
	/* Bring up the dialog */
	gnumeric_dialog_show (state->wb->toplevel,
			      GNOME_DIALOG (dialog), FALSE, TRUE);
}

void
dialog_workbook_attr (Workbook *wb)
{
	GladeXML     *gui;
	AttrState    *state = g_new (AttrState, 1);

	g_return_if_fail (wb != NULL);

	gui = glade_xml_new (GNUMERIC_GLADEDIR "/" GLADE_FILE , NULL);
	if (!gui) {
		g_warning ("Could not find " GLADE_FILE "\n");
		return;
	}

	/* Initialize */
	state->gui		= gui;
	state->wb		= wb;

	attr_dialog_impl (state);
}







/*
 * Gnumeric, the GNOME spreadsheet.
 *
 * Graphics Wizard bootstap file
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 *
 * (C) 1999, 2000 Helix Code, Inc.
 */
#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include <libgnorba/gnorba.h>
#include <glade/glade.h>
#include "gnumeric-util.h"
#include "wizard.h"

#define LAST_PAGE 2

extern void (*graphic_wizard_hook)(Workbook *wb);

static void
customize (GladeXML *gui, WizardGraphicContext *gc)
{
	int i;
	
	/* Now, customize the GUI */
	gc->steps_notebook = GTK_NOTEBOOK (glade_xml_get_widget (gui, "main-notebook"));
	gtk_notebook_set_show_tabs (gc->steps_notebook, FALSE);

	fill_graphic_types (gui, gc);

	for (i = 0; i < 6; i++){
		BonoboViewFrame *view_frame;
		GtkContainer *container;
		GtkWidget *view;
		char *name;

		name = g_strdup_printf ("plot-view-%d", i+1);
		container = GTK_CONTAINER (glade_xml_get_widget (gui, name));
		g_free (name);
		
		view_frame = bonobo_client_site_new_view (gc->client_site);
		view = bonobo_view_frame_get_wrapper (view_frame);
		
		/*
		 * Add the widget to the container.  Remove any
		 * placeholders that might have been left by libglade
		 */
		gtk_container_add (container, view);
	}

	/* Finally, show the widget */
	gtk_widget_show (gc->dialog_toplevel);
}

static void
set_page (WizardGraphicContext *gc, int page)
{
	char *name;
	
	gc->current_page = page;
	gtk_notebook_set_page (gc->steps_notebook, page);

	gtk_widget_set_sensitive (
		glade_xml_get_widget (gc->gui, "button-back"),
		gc->current_page != 0);

	gtk_widget_set_sensitive (
		glade_xml_get_widget (gc->gui, "button-next"),
		gc->current_page != LAST_PAGE);

	switch (gc->current_page){
	case 0:
		name = _("Step 1 of 3: Select graphic type");
		break;
	case 1:
		name = _("Step 2 of 3: Select data ranges");
		break;
	case 2:
		name = _("Step 3 of 3: Customize graphic");
		break;
	default:
		name = "not_reached";
		g_assert_not_reached ();
	}
	gtk_window_set_title (GTK_WINDOW (gc->dialog_toplevel), name);
}

static void
button_back (GtkWidget *widget, WizardGraphicContext *gc)
{
	if (gc->current_page == 0)
		return;
	set_page (gc, gc->current_page - 1);
}

static void
button_next (GtkWidget *widget, WizardGraphicContext *gc)
{
	if (gc->current_page == LAST_PAGE)
		return;
	set_page (gc, gc->current_page + 1);
}

static void
button_cancel (GtkWidget *widget, WizardGraphicContext *gc)
{
	graphic_context_destroy (gc);
	gtk_main_quit ();
}

static void
button_finish (GtkWidget *widget, WizardGraphicContext *gc)
{
}

static void
_connect (GladeXML *gui, const char *widget_name,
	 const char *signal_name,
	 GtkSignalFunc callback, gpointer closure)
{
	gtk_signal_connect (
		GTK_OBJECT (glade_xml_get_widget (gui, widget_name)),
		signal_name,
		callback, closure);
}

#define connect(a,b,c,d,e) _connect(a,b,c, GTK_SIGNAL_FUNC(d),e)

#ifdef WIZARD_PLUGIN
/*
 * No code here yet
 */
#else
static void
boot_wizard (Workbook *wb)
{
}
#endif

void
graphics_wizard (Workbook *wb)
{
	GladeXML *gui;
	WizardGraphicContext *gc;
	GtkWidget *toplevel;
	
	boot_wizard (wb);
	
	gui = glade_xml_new (GNUMERIC_GLADEDIR "/graphics.glade", NULL);
	if (!gui)
		g_error ("Failed to load the interface");

	toplevel = glade_xml_get_widget (gui, "graphics-wizard-dialog");

	gc = graphic_context_new (wb, gui);

	if (!gc){
		gnumeric_notice (
			wb, GNOME_MESSAGE_BOX_ERROR,
			_("Unable to launch the graphics service"));
		gtk_object_unref (GTK_OBJECT (toplevel));
		return;
	}

	/*
	 * Do touchups that are not available from Glade
	 */
	customize (gui, gc);

	/*
	 * Connect signals.
	 */
	connect (gui, "button-back", "clicked", button_back, gc);
	connect (gui, "button-next", "clicked", button_next, gc);
	connect (gui, "button-cancel", "clicked", button_cancel, gc);
	connect (gui, "button-finish", "clicked", button_finish, gc);

	set_page (gc, 0);

	gtk_widget_show (toplevel);
	gtk_window_set_modal (GTK_WINDOW (toplevel), TRUE);
	
	gtk_widget_grab_focus (toplevel);

	gtk_main ();
}

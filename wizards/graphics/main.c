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
#include "workbook.h"
#include "sheet-object.h"
#include <libgnorba/gnorba.h>
#include <glade/glade.h>
#include "gnumeric-util.h"
#include "wizard.h"
#include "graphic-type.h"

#define LAST_PAGE 2

extern void (*graphic_wizard_hook)(Workbook *wb);

BonoboViewFrame *
attach_view (const char *name, WizardGraphicContext *gc)
{
	BonoboViewFrame *view_frame;
	GtkWidget *view;
	GtkContainer *container = GTK_CONTAINER (glade_xml_get_widget (gc->gui, name));

	view_frame = graphic_context_new_chart_view_frame (gc);
	view = bonobo_view_frame_get_wrapper (view_frame);
	
	/*
	 * Add the widget to the container.  Remove any
	 * placeholders that might have been left by libglade
	 */
	gtk_container_add (container, view);
	gtk_widget_show (view);

	return view_frame;
}

static void
customize (GladeXML *gui, WizardGraphicContext *gc)
{
	int i;
	
	/* Now, customize the GUI */
	gtk_notebook_set_show_tabs (gc->steps_notebook, FALSE);
	
	graphic_type_boot (gui, gc);

	for (i = 0; i < 6; i++){
		char *name;
		
		name = g_strdup_printf ("plot-view-%d", i+1);
		attach_view (name, gc);
		g_free (name);
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
	gtk_main_quit ();
}

static void
button_finish (GtkWidget *widget, WizardGraphicContext *gc)
{
	gc->do_create = 1;
	gtk_main_quit ();
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
	
	gui = gnumeric_glade_xml_new (workbook_command_context_gui (wb),
				      "graphics.glade");
        if (gui == NULL)
                return;

	toplevel = glade_xml_get_widget (gui, "graphics-wizard-dialog");

	gc = graphic_context_new (wb, gui);

	if (!gc) {
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

	if (gc->do_create){
		GList *l;
		
		sheet_set_mode_type_full (gc->workbook->current_sheet,
					  SHEET_MODE_CREATE_GRAPHIC, gc->client_site);
#warning super hack
		gtk_object_destroy (gc->dialog_toplevel);

		for (l = gc->data_range_list; l; l = l->next) {
			DataRange *r = l->data;

			sheet_vector_attach (r->vector, gc->workbook->current_sheet);
		}
	} else
		graphic_context_destroy (gc);


	g_warning ("Debugging context_destroy: put back after demo");
/* 	graphic_context_destroy (gc); */
	
}


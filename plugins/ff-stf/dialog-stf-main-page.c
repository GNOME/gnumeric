/*
 * dialog-stf-main-page.c : controls the widget on the main page of the druid
 *
 * Almer. S. Tigelaar <almer1@dds.nl>
 *
 */
 
#include <config.h>
#include <gnome.h>
#include <glade/glade.h>

#include "dialog-stf.h"
#include "stf-util.h"

/*************************************************************************************************
 * MISC UTILITY FUNCTIONS
 *************************************************************************************************/

/**
 * main_page_set_spin_button_adjustment
 * @spinbutton : the spinbutton to adjust
 * @max : the maximum number the user may enter into the spinbutton
 *
 * returns : nothing
 **/
static void
main_page_set_spin_button_adjustment (GtkSpinButton* spinbutton, int max)
{
	GtkAdjustment *spinadjust;

	spinadjust = gtk_spin_button_get_adjustment (spinbutton);
	spinadjust->lower = 1;
	spinadjust->upper = max;
	gtk_spin_button_set_adjustment (spinbutton, spinadjust);
}

/**
 * set_center_prevent_rectangle_size
 * @canvas : canvas where the @rectangle is located on
 * @rectangle : a rectangle on the @canvas which can be used for centering purposes
 * @text : The text item from which the height and width will be used to calculate
 *         the size of the rectangle
 *
 * This is merely a hack to prevent the canvas from centering on the text if the text 
 * width and/or height are smaller than the width and/or height of the GnomeCanvas.
 * Warning 1 : Don't remove this, this is necessary!!
 * Warning 2 : Be sure that the @canvas has both his width and height set to something other than 0
 *
 * returns : nothing
 **/
static void
main_page_set_scroll_region_and_prevent_center (GnomeCanvas *canvas, GnomeCanvasRect *rectangle, GnomeCanvasText *text)
{
	double textwidth, textheight;

	gtk_object_get (GTK_OBJECT (text),
			"text_width", &textwidth,
			"text_height", &textheight,
			NULL);
	textwidth += TEXT_OFFSET;

	stf_set_scroll_region_and_prevent_center (canvas, rectangle, textwidth, textheight);
}

/*************************************************************************************************
 * SIGNAL HANDLERS
 *************************************************************************************************/

/**
 * main_page_startrow_changed
 * @button : the spinbutton the event handler is attached to
 * @func_data : in this case an ImportSrcData_t record
 *
 * This function will adjust the amount of displayed text to 
 * reflect the number of lines the user wants to import
 *
 * returns : nothing
 **/
static void 
main_page_startrow_changed (GtkSpinButton* button, gpointer func_data)
{
	DruidPageData_t *pagedata = func_data;
	const char *cur = pagedata->src->data;
	char *linescaption;
	int startrow;

	startrow = gtk_spin_button_get_value_as_int (button) - 1;

	pagedata->src->lines = pagedata->src->totallines - startrow;

	linescaption = g_strdup_printf (_("%d lines to import"), pagedata->src->lines);
	gtk_label_set_text (pagedata->main_info->main_lines, linescaption);
	g_free (linescaption);

	for (; startrow != 0; startrow--) {
		while (*cur != '\n' && *cur != '\r' && *cur != 0)
			cur++;
		if (*cur == 0) break;
		cur++;
	}
	pagedata->src->cur = cur;
	
	gnome_canvas_item_set (GNOME_CANVAS_ITEM (pagedata->main_info->main_run_text), 
			       "text", cur,
			       NULL);

	main_page_set_scroll_region_and_prevent_center (pagedata->main_info->main_canvas,
							pagedata->main_info->main_run_rect,
							pagedata->main_info->main_run_text);
}

/*************************************************************************************************
 * MAIN EXPORTED FUNCTIONS
 *************************************************************************************************/

/**
 * main_page_init
 * @gui : The glade gui of the dialog
 * @pagedata : pagedata mother struct passed to signal handlers etc.
 *
 * This routine prepares/initializes all widgets on the Main Page of the
 * Druid.
 *
 * returns : nothing
 **/
void
main_page_init (GladeXML *gui, DruidPageData_t *pagedata)
{
	MainInfo_t *info = pagedata->main_info;
	char *label;

	/* Create/get object and fill information struct */

	info->main_separated = GTK_RADIO_BUTTON (glade_xml_get_widget (gui, "main_separated"));
	info->main_fixed     = GTK_RADIO_BUTTON (glade_xml_get_widget (gui, "main_fixed"));
	info->main_startrow  = GTK_SPIN_BUTTON  (glade_xml_get_widget (gui, "main_startrow"));
	info->main_lines     = GTK_LABEL        (glade_xml_get_widget (gui, "main_lines"));
	info->main_frame     = GTK_FRAME        (glade_xml_get_widget (gui, "main_frame"));
	info->main_canvas    = GNOME_CANVAS     (glade_xml_get_widget (gui, "main_canvas"));

	info->main_run_text = GNOME_CANVAS_TEXT (gnome_canvas_item_new (gnome_canvas_root (info->main_canvas),
									GNOME_TYPE_CANVAS_TEXT,
									"text", pagedata->src->data,
									"font", "fixed",
									"x", 0.0,
									"y", 0.0,
									"x_offset", TEXT_OFFSET,
									"anchor", GTK_ANCHOR_NW,
									"clip", FALSE,
									NULL));

	/* Warning : The rectangle is vital to prevent auto-centering, DON'T REMOVE IT! */
    	info->main_run_rect = GNOME_CANVAS_RECT (gnome_canvas_item_new (gnome_canvas_root (info->main_canvas),
									gnome_canvas_rect_get_type (),
									"x1", 0,	"y1", 0,
									"x2", 10,	"y2", 10,
									"width_pixels", (int) 0,
									"fill_color", "white",
									NULL));
	gnome_canvas_item_raise(GNOME_CANVAS_ITEM (info->main_run_rect), 1);
	main_page_set_scroll_region_and_prevent_center (info->main_canvas,
							info->main_run_rect,
							info->main_run_text);

	/* Set properties */

	main_page_set_spin_button_adjustment (info->main_startrow, pagedata->src->lines);

	label = g_strdup_printf (_("%d lines to import"), pagedata->src->lines);
	gtk_label_set_text (info->main_lines, label);
	g_free (label);

        label = g_strdup_printf (_("Example output from %s"), g_basename (pagedata->src->filename));
	gtk_frame_set_label (info->main_frame, label);
	g_free (label);

	/* Connect signals */

	gtk_signal_connect (GTK_OBJECT (info->main_startrow), "changed",
			    GTK_SIGNAL_FUNC (main_page_startrow_changed), 
			    pagedata); 
}
 








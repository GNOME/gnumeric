/*
 * dialog-stf-main-page.c : controls the widget on the main page of the druid
 *
 * Almer. S. Tigelaar <almer1@dds.nl>
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
#include "dialog-stf.h"
#include <gui-util.h>

/*************************************************************************************************
 * MISC UTILITY FUNCTIONS
 *************************************************************************************************/

/**
 * main_page_set_spin_button_adjustment
 * @spinbutton : the spinbutton to adjust
 * @min : the minimum number the user may enter into the spinbutton
 * @max : the maximum number the user may enter into the spinbutton
 *
 * returns : nothing
 **/
static void
main_page_set_spin_button_adjustment (GtkSpinButton* spinbutton, int min, int max)
{
	GtkAdjustment *spinadjust;

	spinadjust = gtk_spin_button_get_adjustment (spinbutton);
	spinadjust->lower = min;
	spinadjust->upper = max;
	gtk_spin_button_set_adjustment (spinbutton, spinadjust);
}

/**
 * main_page_set_scroll_region_and_prevent_center
 * @data : a mother struct
 *
 * This is merely a hack to prevent the canvas from centering on the text if the text
 * width and/or height are smaller than the width and/or height of the GnomeCanvas.
 * Warning 1 : Don't remove this, this is necessary!!
 * Warning 2 : Be sure that the canvas has both his width and height set to something other than 0
 *
 * returns : nothing
 **/
static void
main_page_set_scroll_region_and_prevent_center (DruidPageData_t *data)
{
	MainInfo_t *info = data->main_info;
	double textwidth, textheight;

	gtk_object_get (GTK_OBJECT (info->main_run_text),
			"text_width", &textwidth,
			"text_height", &textheight,
			NULL);
	textwidth += TEXT_OFFSET;

	stf_dialog_set_scroll_region_and_prevent_center (info->main_canvas,
		GNOME_CANVAS_RECT (info->main_run_rect),
		textwidth,
		textheight);
}

/**
 * main_page_import_range_changed
 * @data : mother struct
 *
 * Updates the "number of lines to import" label on the main page of the druid
 *
 * returns : nothing
 **/
static void
main_page_import_range_changed (DruidPageData_t *data)
{
	MainInfo_t *info = data->main_info;
	int startrow, stoprow;
	char *linescaption;

	startrow = gtk_spin_button_get_value_as_int (info->main_startrow);
	stoprow  = gtk_spin_button_get_value_as_int (info->main_stoprow);

	main_page_set_spin_button_adjustment (info->main_startrow, 1, stoprow);
	main_page_set_spin_button_adjustment (info->main_stoprow, startrow, data->lines);

	data->importlines = (stoprow - startrow) + 1;
	linescaption = g_strdup_printf (_("%d lines to import"), data->importlines);
	gtk_label_set_text (info->main_lines, linescaption);
	g_free (linescaption);
}

/*************************************************************************************************
 * SIGNAL HANDLERS
 *************************************************************************************************/

/**
 * main_page_startrow_changed
 * @button : the spinbutton the event handler is attached to
 * @data : mother struct
 *
 * This function will adjust the amount of displayed text to
 * reflect the number of lines the user wants to import
 *
 * returns : nothing
 **/
static void
main_page_startrow_changed (GtkSpinButton* button, DruidPageData_t *data)
{
	const char *cur = data->data;
	int startrow;

	startrow = gtk_spin_button_get_value_as_int (button) - 1;

	for (; startrow != 0; startrow--) {
		while (*cur != '\n' && *cur != 0)
			cur++;
		if (*cur == 0) break;
		cur++;
	}
	data->cur = cur;

	main_page_import_range_changed (data);
	main_page_set_scroll_region_and_prevent_center (data);
}

/**
 * main_page_startrow_changed
 * @button : the spinbutton the event handler is attached to
 * @data : mother struct
 *
 * returns : nothing
 **/
static void
main_page_stoprow_changed (__attribute__((unused)) GtkSpinButton* button,
			   DruidPageData_t *data)
{

	main_page_import_range_changed (data);
	main_page_set_scroll_region_and_prevent_center (data);
}

/**
 * main_page_trim_toggled:
 * @button: the toggle button the event handler is attached to
 * @data: mother struct
 *
 **/
static void
main_page_trim_menu_deactivate (__attribute__((unused)) GtkMenu *menu,
				DruidPageData_t *data)
{
	MainInfo_t *info = data->main_info;

	switch (gtk_option_menu_get_history (info->main_trim)) {
	case 0 : data->trim = (TRIM_TYPE_LEFT | TRIM_TYPE_RIGHT); break;
	case 1 : data->trim = TRIM_TYPE_NEVER; break;
	case 2 : data->trim = TRIM_TYPE_LEFT; break;
	case 3 : data->trim = TRIM_TYPE_RIGHT; break;
	default : g_warning ("Unknown trim type selected");
	}
}

/*************************************************************************************************
 * MAIN EXPORTED FUNCTIONS
 *************************************************************************************************/

/**
 * stf_dialog_main_page_init
 * @gui : The glade gui of the dialog
 * @pagedata : pagedata mother struct passed to signal handlers etc.
 *
 * This routine prepares/initializes all widgets on the Main Page of the
 * Druid.
 *
 * returns : nothing
 **/
void
stf_dialog_main_page_init (GladeXML *gui, DruidPageData_t *pagedata)
{
	MainInfo_t *info = pagedata->main_info;
	char *label, *base;
	const char *s;
	GtkMenu *menu;
	int l, lg;
	int line_count;
	const char *end_point = NULL;
	char *display_data;
	/* Create/get object and fill information struct */

	info->main_separated = GTK_RADIO_BUTTON (glade_xml_get_widget (gui, "main_separated"));
	info->main_fixed     = GTK_RADIO_BUTTON (glade_xml_get_widget (gui, "main_fixed"));
	info->main_startrow  = GTK_SPIN_BUTTON  (glade_xml_get_widget (gui, "main_startrow"));
	info->main_stoprow   = GTK_SPIN_BUTTON  (glade_xml_get_widget (gui, "main_stoprow"));
	info->main_trim      = GTK_OPTION_MENU  (glade_xml_get_widget (gui, "main_trim"));
	info->main_lines     = GTK_LABEL        (glade_xml_get_widget (gui, "main_lines"));
	info->main_frame     = GTK_FRAME        (glade_xml_get_widget (gui, "main_frame"));
	info->main_canvas    = GNOME_CANVAS     (glade_xml_get_widget (gui, "main_canvas"));

	/*
	 * XFREE86 Overflow protection
	 *
	 * There is a bug in XFree86 (at least up until 4.0.3)
	 * which takes down the whole server if very large strings
	 * are drawn on a local display. We therefore simply _not_ display
	 * anything if the string is too large.
	 */
	for (lg = 0, l = 1, s = pagedata->data, line_count = 0; s && *s != '\0'; s++, l++) {
		if (*s == '\n' || *s == '\r' || s[1] == '\0') {
			if (l > lg)
				lg = l;
			l = 0;
			line_count++;
			if (line_count == RAW_LINE_DISPLAY_LIMIT) {
				end_point = s;
				break;
			}
		}
	}
	if (end_point) {
		gint length = end_point -  pagedata->data + 4;
		display_data =  g_strndup (pagedata->data, length);
/*POST RELEASE FIX: we should really append a translated string! */
		display_data[length-4] = '\n';
		display_data[length-3] = '.';
		display_data[length-2] = '.';
		display_data[length-1] = '.';
	} else
		display_data = (char *)pagedata->data;
	info->main_run_text = GNOME_CANVAS_TEXT (gnome_canvas_item_new (gnome_canvas_root (info->main_canvas),
									GNOME_TYPE_CANVAS_TEXT,
									"text", lg < X_OVERFLOW_PROTECT ? display_data : _("LINES TOO LONG!"),
									"font", "fixed",
									"x", 0.0,
									"y", 0.0,
									"x_offset", TEXT_OFFSET,
									"anchor", GTK_ANCHOR_NW,
									NULL));
	if (end_point)
		g_free (display_data);

	/* Warning : The rectangle is vital to prevent auto-centering, DON'T REMOVE IT! */
    	info->main_run_rect = gnome_canvas_item_new (gnome_canvas_root (info->main_canvas),
		GNOME_TYPE_CANVAS_RECT,
		"x1", 0.0,	"y1", 0.0,
		"x2", 10.0,	"y2", 10.0,
		"width_pixels", (int) 0,
		"fill_color", NULL,
		NULL);

	/* Set properties */
	main_page_set_spin_button_adjustment (info->main_startrow, 1, pagedata->lines);
	main_page_set_spin_button_adjustment (info->main_stoprow, 1, pagedata->lines);
	gtk_spin_button_set_value (info->main_stoprow, (float) pagedata->lines);

	main_page_set_scroll_region_and_prevent_center (pagedata);

	base = g_path_get_basename (pagedata->filename);
        label = g_strdup_printf (_("Data (from %s)"), base);
	gtk_frame_set_label (info->main_frame, label);
	g_free (label);
	g_free (base);

	/* Connect signals */
	g_signal_connect (G_OBJECT (info->main_startrow),
		"changed",
		G_CALLBACK (main_page_startrow_changed), pagedata);
	g_signal_connect (G_OBJECT (info->main_stoprow),
		"changed",
		G_CALLBACK (main_page_stoprow_changed), pagedata);
	menu = (GtkMenu *) gtk_option_menu_get_menu (info->main_trim);
	g_signal_connect (G_OBJECT (menu),
		"deactivate",
		G_CALLBACK (main_page_trim_menu_deactivate), pagedata);

	main_page_startrow_changed (info->main_startrow, pagedata);
	main_page_trim_menu_deactivate (menu, pagedata);
}

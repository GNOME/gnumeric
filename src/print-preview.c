/*
 * print.c: Print Preview control for Gnumeric
 *
 * Author:
 *    Miguel de Icaza (miguel@gnu.org)
 */
#include <config.h>
#include <gnome.h>
#include <gdk/gdkkeysyms.h>
#include <libgnomeprint/gnome-printer.h>
#include <libgnomeprint/gnome-print.h>
#include <libgnomeprint/gnome-print-preview.h>

#include "gnumeric.h"
#include "eval.h"
#include "gnumeric-util.h"
#include "gnumeric-sheet.h"
#include "sheet-object.h"
#include "dialogs.h"
#include "main.h"
#include "print-preview.h"
#include "print-info.h"

struct _PrintPreview {
	GnomeApp *toplevel;

	Workbook             *workbook;
	GnomePrintPreviewJob *preview_control;
	GnomePrintContext    *context;
	GtkWidget            *scrolled_window;
	GnomeCanvas          *canvas;
	int                   pages;
};

/*
 * Padding in points around the simulated page
 */
#define PAGE_PAD 4

static void
create_preview_canvas (PrintPreview *pp)
{
	GnomeCanvasItem *i;
	const GnomePaper *paper;
	const char *paper_name;
	
	gtk_widget_push_colormap (gdk_rgb_get_cmap ());
	gtk_widget_push_visual (gdk_rgb_get_visual ());
	
	pp->scrolled_window = gtk_scrolled_window_new (NULL, NULL);
	pp->canvas = GNOME_CANVAS (gnome_canvas_new_aa ());
	gnome_canvas_set_pixels_per_unit (pp->canvas, 1.0);
	gtk_container_add (GTK_CONTAINER (pp->scrolled_window), GTK_WIDGET (pp->canvas));

	/*
	 * Create the preview printing context
	 */
	paper = pp->workbook->print_info->paper;
	paper_name = gnome_paper_name (paper);
	pp->context = gnome_print_preview_new (pp->canvas, paper_name);

	/*
	 * Now add some padding above and below and put a simulated
	 * page on the background
	 */
	i = gnome_canvas_item_new (
		GNOME_CANVAS_GROUP (gnome_canvas_root (pp->canvas)),
		gnome_canvas_rect_get_type (),
		"x1",   	 0.0,
		"y1",   	 0.0,
		"x2",   	 (double) gnome_paper_pswidth (paper),
		"y2",   	 (double) gnome_paper_psheight (paper),
		"fill_color",    "white",
		"outline_color", "black",
		"width_pixels",  1,
		NULL);
	gnome_canvas_item_lower_to_bottom (i);
	i = gnome_canvas_item_new (
		GNOME_CANVAS_GROUP (gnome_canvas_root (pp->canvas)),
		gnome_canvas_rect_get_type (),
		"x1",   	 3.0,
		"y1",   	 3.0,
		"x2",   	 (double) gnome_paper_pswidth (paper) + 3,
		"y2",   	 (double) gnome_paper_psheight (paper) + 3,
		"fill_color",    "black",
		NULL);
	gnome_canvas_item_lower_to_bottom (i);
	gnome_canvas_set_scroll_region (
		pp->canvas,
		0 - PAGE_PAD,
		0 - PAGE_PAD,
		gnome_paper_pswidth (paper) + PAGE_PAD,
		gnome_paper_psheight (paper) + PAGE_PAD);
		
	gtk_widget_show_all (pp->scrolled_window);
	gnome_app_set_contents (pp->toplevel, pp->scrolled_window);
	
	return;
}

static void
preview_destroyed (void *unused, PrintPreview *pp)
{
	gtk_object_unref (GTK_OBJECT (pp->context));
	gtk_object_unref (GTK_OBJECT (pp->preview_control));
}

static void
preview_close_cmd (void *unused, PrintPreview *pp)
{
	gtk_object_destroy (GTK_OBJECT (pp->toplevel));
}

static void
preview_file_print_cmd (void *unused, PrintPreview *pp)
{
	workbook_print (pp->workbook, FALSE);
}

static void
preview_first_page_cmd (void *unused, PrintPreview *pp)
{
	gnome_print_preview_job_page_show (pp->preview_control, 0);
}

static void
preview_next_page_cmd (void *unused, PrintPreview *pp)
{
	int n = gnome_print_preview_job_current_page (pp->preview_control);

	if (n+2 > pp->pages)
		return;
	gnome_print_preview_job_page_show (pp->preview_control, n+1);
}

static void
preview_prev_page_cmd (void *unused, PrintPreview *pp)
{
	int n = gnome_print_preview_job_current_page (pp->preview_control);

	if (n < 1)
		return;
	gnome_print_preview_job_page_show (pp->preview_control, n-1);
}

static void
preview_last_page_cmd (void *unused, PrintPreview *pp)
{
	gnome_print_preview_job_page_show (pp->preview_control, pp->pages-1);
}

static void
preview_zoom_in_cmd (void *unused, PrintPreview *pp)
{
	gnome_canvas_set_pixels_per_unit (
		pp->canvas,
		pp->canvas->pixels_per_unit + 0.25);
}

static void
preview_zoom_out_cmd (void *unused, PrintPreview *pp)
{
	gnome_canvas_set_pixels_per_unit (
		pp->canvas,
		pp->canvas->pixels_per_unit - 0.25);
}

static GnomeUIInfo preview_file_menu [] = {
	GNOMEUIINFO_MENU_PRINT_ITEM (preview_file_print_cmd, NULL),
	GNOMEUIINFO_MENU_CLOSE_ITEM (preview_close_cmd, NULL),
	GNOMEUIINFO_END
};

static GnomeUIInfo preview_edit_menu [] = {
	GNOMEUIINFO_ITEM_STOCK (
		N_("_First page"), N_("Shows the first page"),
		preview_first_page_cmd, GNOME_STOCK_PIXMAP_FIRST),
	GNOMEUIINFO_ITEM_STOCK (
		N_("_Previous page"), N_("Shows the previous page"),
		preview_prev_page_cmd, GNOME_STOCK_PIXMAP_BACK),
	GNOMEUIINFO_ITEM_STOCK (
		N_("_Next page"), N_("Shows the next page"),
		preview_next_page_cmd, GNOME_STOCK_PIXMAP_FORWARD),
	GNOMEUIINFO_ITEM_STOCK (
		N_("_Last page"), N_("Shows the last page"),
		preview_last_page_cmd, GNOME_STOCK_PIXMAP_LAST),

	GNOMEUIINFO_SEPARATOR,

	{ GNOME_APP_UI_ITEM, N_("Zoom _in"), N_("Zooms in"), preview_zoom_in_cmd },
	{ GNOME_APP_UI_ITEM, N_("Zoom _out"), N_("Zooms out"), preview_zoom_out_cmd },

	GNOMEUIINFO_END
};

static GnomeUIInfo top_menu [] = {
	GNOMEUIINFO_MENU_FILE_TREE (preview_file_menu),
	GNOMEUIINFO_MENU_EDIT_TREE (preview_edit_menu),
	GNOMEUIINFO_END
};

static void
create_toplevel (PrintPreview *pp)
{
	GtkWidget *toplevel;
	char *name;
	gint width, height;
	const GnomePaper *paper;
	
	name = g_strdup_printf (_("Preview for %s"),
				pp->workbook->filename ?
				pp->workbook->filename :
				_("the workbook"));
	toplevel = gnome_app_new ("Gnumeric", name);
	g_free (name);

	paper = pp->workbook->print_info->paper;
	width = gnome_paper_pswidth (paper) + PAGE_PAD * 3;
	height = gnome_paper_psheight (paper) + PAGE_PAD * 3;
	
	if (width > gdk_screen_width ()-40)
		width = gdk_screen_width ()-40;

	if (height > gdk_screen_height ()-100)
		height = gdk_screen_height ()-100;
	
	gtk_widget_set_usize (toplevel, width, height);
	gtk_window_set_policy (GTK_WINDOW (toplevel), TRUE, TRUE, FALSE);

	pp->toplevel = GNOME_APP (toplevel);

	gnome_app_create_menus_with_data (pp->toplevel, top_menu, pp);

	gtk_signal_connect (
		GTK_OBJECT (pp->toplevel), "destroy",
		GTK_SIGNAL_FUNC (preview_destroyed), pp);
}

GnomePrintContext *
print_preview_get_print_context (PrintPreview *pp)
{
	g_return_val_if_fail (pp != NULL, NULL);
	
	return pp->context;
}

static void
workbook_destroyed (void *unused, PrintPreview *pp)
{
	gtk_object_destroy (GTK_OBJECT (pp->toplevel));
}

PrintPreview *
print_preview_new (Workbook *wb)
{
	PrintPreview *pp;

	g_return_val_if_fail (wb != NULL, NULL);
	g_return_val_if_fail (IS_WORKBOOK (wb), NULL);
	
	pp = g_new0 (PrintPreview, 1);

	pp->workbook = wb;
	create_toplevel (pp);
	create_preview_canvas (pp);

	gtk_signal_connect (
		GTK_OBJECT (pp->workbook), "destroy",
		GTK_SIGNAL_FUNC (workbook_destroyed), pp);

	{
		static int warning_shown;

		if (!warning_shown){
			gnumeric_notice (
				wb,
				GNOME_MESSAGE_BOX_WARNING,
				_("The Print Preview feature is being developed.\n"
				  "The results of the preview is not correct currently,\n"
				  "it might include a buggy rendering (like black\n"
				  "blocks or an incorrect text placement).\n\n"
				  "We apologize for the inconvenience"));
		}
		warning_shown = 1;
	}
	return pp;
}

void
print_preview_print_done (PrintPreview *pp)
{
	g_return_if_fail (pp != NULL);

	pp->preview_control = gnome_print_preview_get_job (
		GNOME_PRINT_PREVIEW (pp->context));

	pp->pages = gnome_print_preview_job_num_pages (pp->preview_control);
	
	gtk_widget_show (GTK_WIDGET (pp->toplevel));
}


/*
 * print.c: Print Preview control for Gnumeric
 *
 * Author:
 *    Miguel de Icaza (miguel@gnu.org)
 *
 * Given the large memory usage of an entire sheet on
 * a canvas, we have now taken a new approach: we keep in
 * a GNOME Print Metafile each page.  And we render this
 * metafile into the printing context on page switch.
 */
#include <config.h>
#include <gnome.h>
#include <gdk/gdkkeysyms.h>
#include <libgnomeprint/gnome-printer.h>
#include <libgnomeprint/gnome-print.h>
#include <libgnomeprint/gnome-print-meta.h>
#include <libgnomeprint/gnome-print-preview.h>

#include "gnumeric.h"
#include "eval.h"
#include "gnumeric-util.h"
#include "gnumeric-sheet.h"
#include "sheet-object.h"
#include "dialogs.h"
#include "main.h"
#include "print.h"
#include "print-info.h"
#include "print-preview.h"
#include "cursors.h"
#include "pixmaps/zoom_in.xpm"
#include "pixmaps/zoom_out.xpm"

#define TOOLBAR_BUTTON_BASE 4
#define MOVE_INDEX          4

typedef enum {
	MODE_MOVE_ON_CLICK,
	MODE_MOVING,
	MODE_ZOOM_IN,
	MODE_ZOOM_OUT
} PreviewMode;
	
struct _PrintPreview {
	GnomeApp *toplevel;

	Sheet                *sheet;

	/*
	 * printing contexts:
	 */
	GnomePrintContext    *preview;
	GnomePrintContext    *metafile;
	
	/*
	 * Preview canvas
	 */
	GtkWidget            *scrolled_window;
	GnomeCanvas          *canvas;

	/*
	 * status display
	 */
	GtkWidget            *page_entry;
	GtkWidget            *last;

	int                  current_page;
	int                  pages;

	GnomeUIInfo          *toolbar;

	PreviewMode          mode;

	/*
	 * Used for dragging the sheet
	 */
	double               base_x, base_y;

	/*
	 * Signal ID for the sheet destroy watcher
	 */
	guint                destroy_id;
};

/*
 * Padding in points around the simulated page
 */
#define PAGE_PAD 4

static void
render_page (PrintPreview *pp, int page)
{
	const GnomePaper *paper;
	const char *paper_name;

	gtk_object_unref (GTK_OBJECT (pp->preview));
	pp->preview = NULL;

	/*
	 * Create the preview printing context
	 */
	paper = pp->sheet->print_info->paper;
	paper_name = gnome_paper_name (paper);
	pp->preview = gnome_print_preview_new (pp->canvas, paper_name);

	/*
	 * Reset scrolling region
	 */
	gnome_canvas_set_scroll_region (
		pp->canvas,
		0 - PAGE_PAD,
		0 - PAGE_PAD,
		gnome_paper_pswidth (paper) + PAGE_PAD,
		gnome_paper_psheight (paper) + PAGE_PAD);

	gnome_print_meta_render_from_object_page (pp->preview, GNOME_PRINT_META (pp->metafile), page);
}

static void
goto_page (PrintPreview *pp, int page)
{
	char *text;

	if (page == pp->current_page)
		return;

	pp->current_page = page;
	
	text = g_strdup_printf ("%d", page+1);
	gtk_entry_set_text (GTK_ENTRY (pp->page_entry), text);
	g_free (text);

	render_page (pp, page);
}

static void
change_page_cmd (GtkEntry *entry, PrintPreview *pp)
{
	char *text = gtk_entry_get_text (entry);
	int p;
	
	p = atoi (text);
	p--;
	if (p < 0){
		goto_page (pp, 0);
		return;
	}
	if (p > pp->pages){
		goto_page (pp, pp->pages-1);
		return;
	}
	goto_page (pp, p);
}

static void
do_zoom (PrintPreview *pp, int factor)
{
	double change = (pp->canvas->pixels_per_unit / 2) * factor;
	
	gnome_canvas_set_pixels_per_unit (
		pp->canvas,
		pp->canvas->pixels_per_unit + change);
	render_page (pp, pp->current_page);
}

static int
preview_canvas_event (GnomeCanvas *canvas, GdkEvent *event, PrintPreview *pp)
{
	switch (event->type){
	case GDK_BUTTON_PRESS:
		if (pp->mode == MODE_MOVE_ON_CLICK){
			gtk_grab_add (GTK_WIDGET (canvas));
			pp->mode = MODE_MOVING;
			gnome_canvas_w2c_d (
				canvas, event->button.x, event->button.y,
				&pp->base_x, &pp->base_y);
			break;
		} 
		break;
		
	case GDK_BUTTON_RELEASE:
		if (pp->mode == MODE_MOVING){
			gtk_grab_remove (GTK_WIDGET (canvas));
			pp->mode = MODE_MOVE_ON_CLICK;
			break;
		} else if (pp->mode == MODE_ZOOM_OUT){
			do_zoom (pp, -1);
		} else if (pp->mode == MODE_ZOOM_IN){
			do_zoom (pp, 1);
		}
		break;
		
	case GDK_MOTION_NOTIFY:
		if (pp->mode == MODE_MOVING){
			GtkAdjustment *va, *ha;
			double dx, dy;
			double ex, ey;
			
			va = gtk_scrolled_window_get_vadjustment (
				GTK_SCROLLED_WINDOW (pp->scrolled_window));
			ha = gtk_scrolled_window_get_hadjustment (
				GTK_SCROLLED_WINDOW (pp->scrolled_window));

			gnome_canvas_w2c_d (canvas,
					    event->motion.x, event->motion.y,
					    &ex, &ey);
			
			dx = ex - pp->base_x;
			dy = ey - pp->base_y;
			gtk_adjustment_set_value (
				va,
				CLAMP (va->value + dy, va->lower, va->upper - va->page_size));
			gtk_adjustment_set_value (
				ha,
				CLAMP (ha->value + dx, ha->lower, ha->upper - ha->page_size));

			pp->base_x = ex;
			pp->base_y = ey;
			break;
		}
		break;

	default:
		return FALSE;
	}
	return TRUE;
}

static void
create_preview_canvas (PrintPreview *pp)
{
	GnomeCanvasItem *i;
	GtkWidget *box, *status;
	const GnomePaper *paper;
	const char *paper_name;
	
	gtk_widget_push_colormap (gdk_rgb_get_cmap ());
	gtk_widget_push_visual (gdk_rgb_get_visual ());
	
	pp->scrolled_window = gtk_scrolled_window_new (NULL, NULL);
	pp->canvas = GNOME_CANVAS (gnome_canvas_new_aa ());
	gnome_canvas_set_pixels_per_unit (pp->canvas, 1.0);
	gtk_signal_connect (GTK_OBJECT (pp->canvas), "event",
			    GTK_SIGNAL_FUNC (preview_canvas_event), pp);
	
	gtk_container_add (GTK_CONTAINER (pp->scrolled_window), GTK_WIDGET (pp->canvas));

	/*
	 * Create the preview printing context
	 */
	paper = pp->sheet->print_info->paper;
	paper_name = gnome_paper_name (paper);
	pp->preview = gnome_print_preview_new (pp->canvas, paper_name);

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

	box = gtk_vbox_new (FALSE, 0);
	status = gtk_hbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (status), gtk_label_new (_("Page: ")), FALSE, FALSE, 0);
	pp->page_entry = gtk_entry_new ();
	gtk_widget_set_usize (pp->page_entry, 40, 0);
	gtk_signal_connect (GTK_OBJECT (pp->page_entry), "activate", change_page_cmd, pp);
	gtk_box_pack_start (GTK_BOX (status), pp->page_entry, FALSE, FALSE, 0);
	pp->last = gtk_label_new ("");
	gtk_box_pack_start (GTK_BOX (status), pp->last, FALSE, FALSE, 0);
	
	gtk_box_pack_start (GTK_BOX (box), status, FALSE, FALSE, 3);
	gtk_box_pack_start (GTK_BOX (box), pp->scrolled_window, TRUE, TRUE, 0);
	gnome_app_set_contents (pp->toplevel, box);
	gtk_widget_show_all (box);
	
	return;
}

/*
 * Invoked when the toplevel is destroyed, free our resources
 */
static void
preview_destroyed (void *unused, PrintPreview *pp)
{
	gtk_signal_disconnect (GTK_OBJECT (pp->sheet), pp->destroy_id);
	gtk_object_unref (GTK_OBJECT (pp->preview));
	gtk_object_unref (GTK_OBJECT (pp->metafile));
	g_free (pp->toolbar);
	g_free (pp);
}

static void
preview_close_cmd (void *unused, PrintPreview *pp)
{
	gtk_object_destroy (GTK_OBJECT (pp->toplevel));
}

static void
preview_file_print_cmd (void *unused, PrintPreview *pp)
{
	sheet_print (pp->sheet, FALSE, PRINT_ACTIVE_SHEET);
}

static void
preview_first_page_cmd (void *unused, PrintPreview *pp)
{
	goto_page (pp, 0);
}

static void
preview_next_page_cmd (void *unused, PrintPreview *pp)
{
	int current_page = pp->current_page;

	if (current_page+2 > pp->pages)
		return;
	goto_page (pp, current_page+1);
}

static void
preview_prev_page_cmd (void *unused, PrintPreview *pp)
{
	int current_page = pp->current_page;

	if (current_page < 1)
		return;
	goto_page (pp, current_page-1);
}

static void
preview_last_page_cmd (void *unused, PrintPreview *pp)
{
	goto_page (pp, pp->pages-1);
}

static void
zoom_state (PrintPreview *pp, int toolbar_button)
{
	int i;
	
	for (i = 0; i < 3; i++){
		gtk_toggle_button_set_active (
			GTK_TOGGLE_BUTTON (pp->toolbar [i + TOOLBAR_BUTTON_BASE].widget),
			i == toolbar_button);
	}
}

static void
preview_zoom_in_cmd (GtkToggleButton *t, PrintPreview *pp)
{
	if (t->active){
		zoom_state (pp, 1);
		cursor_set_widget (pp->canvas, GNUMERIC_CURSOR_ZOOM_IN);
		pp->mode = MODE_ZOOM_IN;
	}
}

static void
preview_zoom_out_cmd (GtkToggleButton *t, PrintPreview *pp)
{
	if (t->active){
		zoom_state (pp, 2);
		cursor_set_widget (pp->canvas, GNUMERIC_CURSOR_ZOOM_OUT);
		pp->mode = MODE_ZOOM_OUT;
	}
}

static void
move_cmd (GtkToggleButton *t, PrintPreview *pp)
{
	if (t->active){
		zoom_state (pp, 0);
		cursor_set_widget (pp->canvas, GNUMERIC_CURSOR_MOVE);
		pp->mode = MODE_MOVE_ON_CLICK;
	}
}
 
static GnomeUIInfo preview_file_menu [] = {
	GNOMEUIINFO_MENU_PRINT_ITEM (preview_file_print_cmd, NULL),
	GNOMEUIINFO_MENU_CLOSE_ITEM (preview_close_cmd, NULL),
	GNOMEUIINFO_END
};

static GnomeUIInfo preview_view_menu [] = {
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
	{ GNOME_APP_UI_SUBTREE, N_("_View"), NULL, preview_view_menu },
	GNOMEUIINFO_END
};

static GnomeUIInfo toolbar [] = {
	GNOMEUIINFO_ITEM_STOCK (
		N_("First"), N_("Shows the first page"),
		preview_first_page_cmd, GNOME_STOCK_PIXMAP_FIRST),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Back"), N_("Shows the previous page"),
		preview_prev_page_cmd, GNOME_STOCK_PIXMAP_BACK),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Next"), N_("Shows the next page"),
		preview_next_page_cmd, GNOME_STOCK_PIXMAP_FORWARD),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Last"), N_("Shows the last page"),
		preview_last_page_cmd, GNOME_STOCK_PIXMAP_LAST),

	{ GNOME_APP_UI_TOGGLEITEM, N_("Move"), N_("Move"), move_cmd },
	GNOMEUIINFO_TOGGLEITEM(N_("Zoom in"), N_("Zooms the page in"), preview_zoom_in_cmd, zoom_in_xpm),
	GNOMEUIINFO_TOGGLEITEM(N_("Zoom out"), N_("Zooms the page out"), preview_zoom_out_cmd, zoom_out_xpm),
	GNOMEUIINFO_END
};

static void
create_toplevel (PrintPreview *pp)
{
	GtkWidget *toplevel;
	char *name, *txta, *txtb;
	gint width, height;
	const GnomePaper *paper;
	
	g_return_if_fail (pp != NULL);
	g_return_if_fail (pp->sheet != NULL);
	g_return_if_fail (pp->sheet->workbook != NULL);
	g_return_if_fail (pp->sheet->print_info != NULL);

	txta = pp->sheet->name;
	txtb = pp->sheet->workbook->filename;
	name = g_strdup_printf (_("Preview for %s in %s"),
				txta ? txta : _("the sheet"),
				txtb ? txtb : _("the workbook"));
	toplevel = gnome_app_new ("Gnumeric", name);
	g_free (name);

	paper  = pp->sheet->print_info->paper;
	width  = gnome_paper_pswidth  (paper) + PAGE_PAD * 3;
	height = gnome_paper_psheight (paper) + PAGE_PAD * 3;
	
	if (width > gdk_screen_width () - 40)
		width = gdk_screen_width () - 40;

	if (height > gdk_screen_height () - 100)
		height = gdk_screen_height () - 100;
	
	gtk_widget_set_usize (toplevel, width, height);
	gtk_window_set_policy (GTK_WINDOW (toplevel), TRUE, TRUE, FALSE);

	pp->toplevel = GNOME_APP (toplevel);

	gnome_app_create_menus_with_data (pp->toplevel, top_menu, pp);

	pp->toolbar = g_malloc (sizeof (toolbar));
	memcpy (pp->toolbar, toolbar, sizeof (toolbar));
	
	gnome_app_create_toolbar_with_data (pp->toplevel, pp->toolbar, pp);
	
	gtk_signal_connect (
		GTK_OBJECT (pp->toplevel), "destroy",
		GTK_SIGNAL_FUNC (preview_destroyed), pp);
}

GnomePrintContext *
print_preview_context (PrintPreview *pp)
{
	GnomePrintMeta *meta;
	
	g_return_val_if_fail (pp != NULL, NULL);

	meta = gnome_print_meta_new ();

	pp->metafile = GNOME_PRINT_CONTEXT (meta);

	return pp->metafile;
}

static void
sheet_destroyed (void *unused, PrintPreview *pp)
{
	gtk_object_destroy (GTK_OBJECT (pp->toplevel));
}

PrintPreview *
print_preview_new (Sheet *sheet)
{
	PrintPreview *pp;

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	
	pp = g_new0 (PrintPreview, 1);

	pp->sheet = sheet;
	
	create_toplevel (pp);
	create_preview_canvas (pp);

	pp->destroy_id = gtk_signal_connect (
		GTK_OBJECT (pp->sheet), "destroy",
		GTK_SIGNAL_FUNC (sheet_destroyed), pp);

	{
		static int warning_shown;

		if (!warning_shown) {
			gnumeric_notice (
				sheet->workbook,
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
	char *text;
	
	g_return_if_fail (pp != NULL);

	gtk_widget_show (GTK_WIDGET (pp->toplevel));

	pp->current_page = -1;
	pp->pages = gnome_print_meta_pages (GNOME_PRINT_META (pp->metafile));
	
	goto_page (pp, 0);

	text = g_strdup_printf ("/%d", pp->pages);
	gtk_label_set_text (GTK_LABEL (pp->last), text);
	g_free (text);

	gtk_toggle_button_set_active (
		GTK_TOGGLE_BUTTON (pp->toolbar [MOVE_INDEX].widget), TRUE);
}


/* vim: set sw=8: */
/*
 * item-edit.c : Edit facilities for worksheets.
 *
 * (C) 1999-2001 Miguel de Icaza & Jody Goldberg
 *
 * This module provides:
 *   * Integration of an in-sheet text editor (GtkEntry) with the Workbook
 *     GtkEntry as a canvas item.
 *
 *   * Feedback on expressions in the spreadsheet (referenced cells or
 *     ranges are highlighted on the spreadsheet).
 */
#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include "gnumeric.h"
#include "item-edit.h"

#include "item-cursor.h"
#include "gnumeric-canvas.h"
#include "sheet-control-gui-priv.h"
#include "sheet.h"
#include "sheet-style.h"
#include "value.h"
#include "ranges.h"
#include "style.h"
#include "parse-util.h"
#include "sheet-merge.h"
#include "workbook.h"
#include "workbook-edit.h"
#include "gui-util.h"
#include "widgets/gnumeric-expr-entry.h"
#include "item-debug.h"
#define GNUMERIC_ITEM "EDIT"

#include <gsf/gsf-impl-utils.h>
#include <gal/widgets/e-cursors.h>
#include <ctype.h>
#include <string.h>

static GnomeCanvasItem *item_edit_parent_class;

/* The arguments we take */
enum {
	ARG_0,
	ARG_SHEET_CONTROL_GUI,	/* The SheetControlGUI * argument */
};

static gboolean
point_is_inside_range (ItemEdit *item_edit, const char *text, Range *range)
{
	Sheet *sheet = ((SheetControl *) item_edit->scg)->sheet;
	Sheet *parse_sheet;

	return (gnm_expr_entry_get_rangesel (GNUMERIC_EXPR_ENTRY 
					     (gtk_widget_get_parent (
						     GTK_WIDGET (item_edit->entry))), 
					     range, &parse_sheet) && (parse_sheet == sheet));
}

static void
entry_create_feedback_range (ItemEdit *item_edit, Range *r)
{
	GnomeCanvasItem *item = GNOME_CANVAS_ITEM (item_edit);

	if (!item_edit->feedback_cursor)
		item_edit->feedback_cursor = gnome_canvas_item_new (
			GNOME_CANVAS_GROUP (item->canvas->root),
			item_cursor_get_type (),
			"SheetControlGUI",  item_edit->scg,
			"Style",  ITEM_CURSOR_BLOCK,
			"Color",  "red",
			NULL);

	item_cursor_bound_set (ITEM_CURSOR (item_edit->feedback_cursor), r);
}

static void
entry_destroy_feedback_range (ItemEdit *item_edit)
{
	if (item_edit->feedback_cursor){
		gtk_object_destroy (GTK_OBJECT (item_edit->feedback_cursor));
		item_edit->feedback_cursor = NULL;
	}
}

/* WARNING : DO NOT CALL THIS FROM FROM UPDATE.  It may create another
 *           canvas-item which would in turn call update and confuse the
 *           canvas.
 */
static void
scan_for_range (ItemEdit *item_edit, const char *text)
{
	Range range;

	if (point_is_inside_range (item_edit, text, &range)){
		if (!item_edit->feedback_disabled)
			entry_create_feedback_range (item_edit, &range);
	} else
		entry_destroy_feedback_range (item_edit);
}

static void
item_edit_draw (GnomeCanvasItem *item, GdkDrawable *drawable,
		int x, int y, int width, int height)
{
	GtkWidget *canvas   = GTK_WIDGET (item->canvas);
	ItemEdit *item_edit = ITEM_EDIT (item);
	SheetControl *sc = (SheetControl *) item_edit->scg;
	ColRowInfo const *ci = sheet_col_get_info (sc->sheet,
						   item_edit->pos.col);
	int const left_pos = ((int)item->x1) + ci->margin_a - x;

	/* NOTE : This does not handle vertical alignment yet so there may be some
	 * vertical jumping when edit.
	 */
	int top_pos = ((int)item->y1) - y + 1; /* grid line */
	int cursor_pos = gtk_editable_get_position (GTK_EDITABLE (item_edit->entry));
	char const *text;
	StyleFont	*style_font = item_edit->style_font;
	PangoLayout	*layout;
	PangoAttrList	*attrs;
	PangoRectangle	 pos;

	if (item_edit->style_font == NULL)
		return;

       	/* Draw the background (recall that gdk_draw_rectangle excludes far coords) */
	gdk_draw_rectangle (
		drawable, canvas->style->white_gc, TRUE,
		((int)item->x1)-x, ((int)item->y1)-y,
		(int)(item->x2-item->x1),
		(int)(item->y2-item->y1));

	/* copy the layout from the entry so that we get the text */
	layout = pango_layout_copy (gtk_entry_get_layout (item_edit->entry));
	pango_layout_set_font_description (layout,
		pango_context_get_font_description (style_font->pango.context));
	pango_layout_set_wrap (layout, PANGO_WRAP_CHAR);
	pango_layout_set_width (layout, (int)(item->x2-item->x1)*PANGO_SCALE);
	pango_layout_get_pixel_size (layout, &width, &height);

 	attrs = pango_attr_list_new();
	pango_layout_set_attributes (layout, attrs);

	text = wbcg_edit_get_display_text (item_edit->scg->wbcg);
	pango_layout_index_to_pos (layout,
		g_utf8_offset_to_pointer (text, cursor_pos) - text, &pos);
	gdk_draw_layout (drawable, canvas->style->black_gc,
		left_pos, top_pos, layout);
	if (item_edit->cursor_visible)
		gdk_draw_line (drawable, canvas->style->black_gc,
			left_pos + PANGO_PIXELS (pos.x), top_pos + PANGO_PIXELS (pos.y),
			left_pos + PANGO_PIXELS (pos.x), top_pos + PANGO_PIXELS (pos.y + pos.height));
	g_object_unref (G_OBJECT (layout));
}

static double
item_edit_point (GnomeCanvasItem *item, double c_x, double c_y, int cx, int cy,
		 GnomeCanvasItem **actual_item)
{
	*actual_item = NULL;
	if ((cx < item->x1) || (cy < item->y1) || (cx >= item->x2) || (cy >= item->y2))
		return 10000.0;

	*actual_item = item;
	return 0.0;
}

static int
item_edit_event (GnomeCanvasItem *item, GdkEvent *event)
{
	/* FIXME : Handle mouse events here */
	switch (event->type){
	case GDK_ENTER_NOTIFY:
		e_cursor_set_widget (item->canvas, E_CURSOR_XTERM);
		return TRUE;
	default :
		break;
	}
	return FALSE;
}

static void
recalc_spans (GnomeCanvasItem *item)
{
	ItemEdit *item_edit = ITEM_EDIT (item);
	GnumericCanvas *gcanvas = GNUMERIC_CANVAS (item->canvas);
	PangoLayout* layout;
	int width,height;
	int col_size;
	ColRowInfo const *cri;
	Sheet    *sheet  = sc_sheet (SHEET_CONTROL (item_edit->scg));
	StyleFont *style_font = item_edit->style_font;
	Range const *merged;
	int col_span,row_span,tmp;
	int cur_col;
	
	cur_col = item_edit->pos.col;
	cri = sheet_col_get_info (sheet, cur_col);

	g_return_if_fail (cri != NULL);

	layout=pango_layout_copy(gtk_entry_get_layout(item_edit->entry));
	pango_layout_set_font_description(layout,pango_context_get_font_description(style_font->pango.context));
	pango_layout_set_wrap(layout,PANGO_WRAP_CHAR);
	pango_layout_set_width(layout,-1);
	pango_layout_get_pixel_size(layout,&width,&height);

	/* Start after the grid line and the left margin */
	col_size = cri->size_pixels - cri->margin_a - 1;

	while(col_size<width){
		cur_col++;
		if (cur_col > gcanvas->last_full.col || cur_col >= SHEET_MAX_COLS) {
			cur_col--;
			pango_layout_set_width(layout,col_size*PANGO_SCALE);
			break;
		}
		cri = sheet_col_get_info (sheet, cur_col);
		g_return_if_fail (cri != NULL);
		if(cri->visible)
			col_size += cri->size_pixels;
	}
	pango_layout_get_pixel_size(layout,&width,&height);
	g_object_unref(layout);
	col_span = 1 + cur_col - item_edit->pos.col;
	merged = sheet_merge_is_corner (sheet, &item_edit->pos);
	if (merged != NULL) {
		int tmp = merged->end.col - merged->start.col + 1;
		if (col_span < tmp)
			col_span = tmp;
		row_span = merged->end.row - merged->start.row + 1;
	} else
		row_span = 1;

	/* The lower right is based on the span size excluding the grid lines
	 * Recall that the bound excludes the far point
	 */
	item->x2 = 1 + item->x1 - 2 +
		scg_colrow_distance_get (item_edit->scg, TRUE, item_edit->pos.col,
					 item_edit->pos.col + col_span);

	tmp = scg_colrow_distance_get (item_edit->scg, FALSE, item_edit->pos.row,
				       item_edit->pos.row + row_span) - 2;
	item->y2 = 1 + item->y1 + MAX (height, tmp);

}
	

static void
item_edit_update (GnomeCanvasItem *item, double *affine, ArtSVP *clip_path, int flags)
{
	ItemEdit *item_edit = ITEM_EDIT (item);

	if (GNOME_CANVAS_ITEM_CLASS (item_edit_parent_class)->update)
		(*GNOME_CANVAS_ITEM_CLASS(item_edit_parent_class)->update)(item, affine, clip_path, flags);

	/* do not calculate spans until after row/col has been set */
	if (item_edit->style_font != NULL) {
		/* Redraw before and after in case the span changes */
		gnome_canvas_request_redraw (GNOME_CANVAS_ITEM (item_edit)->canvas,
					     item->x1, item->y1, item->x2, item->y2);
		recalc_spans (item);
		/* Redraw before and after in case the span changes */
		gnome_canvas_request_redraw (GNOME_CANVAS_ITEM (item_edit)->canvas,
					     (int)item->x1, (int)item->y1, (int)item->x2, (int)item->y2);
	}
}

static int
cb_item_edit_cursor_blink (ItemEdit *item_edit)
{
	GnomeCanvasItem *item = GNOME_CANVAS_ITEM (item_edit);

	item_edit->cursor_visible = !item_edit->cursor_visible;

	gnome_canvas_item_request_update (item);
	return TRUE;
}

static void
item_edit_cursor_blink_stop (ItemEdit *item_edit)
{
	if (item_edit->blink_timer == -1)
		return;

	gtk_timeout_remove (item_edit->blink_timer);
	item_edit->blink_timer = -1;
}

static void
item_edit_cursor_blink_start (ItemEdit *item_edit)
{
	item_edit->blink_timer = gtk_timeout_add (
		500, (GtkFunction)(cb_item_edit_cursor_blink),
		item_edit);
}

/*
 * Instance initialization
 */
static void
item_edit_init (ItemEdit *item_edit)
{
	GnomeCanvasItem *item = GNOME_CANVAS_ITEM (item_edit);

	item->x1 = 0;
	item->y1 = 0;
	item->x2 = 1;
	item->y2 = 1;

	item_edit->scg = NULL;
	item_edit->pos.col = -1;
	item_edit->pos.row = -1;
	item_edit->style_font = NULL;
	item_edit->cursor_visible = TRUE;
	item_edit->feedback_disabled = FALSE;
	item_edit->feedback_cursor = NULL;
}

/*
 * Invoked when the GtkEntry has changed
 *
 * We use this to sync up the GtkEntry with our display on the screen.
 */
static void
entry_changed (GnumericExprEntry *ignore, void *data)
{
	GnomeCanvasItem *item = GNOME_CANVAS_ITEM (data);
	ItemEdit *item_edit = ITEM_EDIT (item);
	char const *text = gtk_entry_get_text (item_edit->entry);

	if (gnumeric_char_start_expr_p (text))
		scan_for_range (item_edit, text);

	gnome_canvas_item_request_update (item);
}

static void
item_edit_destroy (GtkObject *o)
{
	ItemEdit *item_edit = ITEM_EDIT (o);
	GtkEntry *entry = item_edit->entry;

	item_edit_cursor_blink_stop (item_edit);
	entry_destroy_feedback_range (item_edit);

	if (item_edit->signal_changed != 0) {
		g_signal_handler_disconnect 
			(G_OBJECT (gtk_widget_get_parent (GTK_WIDGET (entry))), 
			 item_edit->signal_changed);
		g_signal_handler_disconnect (GTK_OBJECT (entry), item_edit->signal_key_press);
		g_signal_handler_disconnect (GTK_OBJECT (entry), item_edit->signal_button_press);
		item_edit->signal_changed = 0;
	}
	scg_set_display_cursor (item_edit->scg);

	if (GTK_OBJECT_CLASS (item_edit_parent_class)->destroy)
		(*GTK_OBJECT_CLASS (item_edit_parent_class)->destroy)(o);
}

static int
entry_event (GtkEntry *entry, GdkEvent *event, GnomeCanvasItem *item)
{
	entry_changed (NULL, item);
	return TRUE;
}

static int
entry_cursor_event (GtkEntry *entry, GParamSpec *pspec, GnomeCanvasItem *item)
{
	entry_changed (NULL, item);
	return TRUE;
}

static void
item_edit_set_arg (GtkObject *o, GtkArg *arg, guint arg_id)
{
	GnomeCanvasItem *item      = GNOME_CANVAS_ITEM (o);
	ItemEdit        *item_edit = ITEM_EDIT (o);
	GnumericCanvas  *gcanvas    = GNUMERIC_CANVAS (item->canvas);
	SheetView const	*sv;
	GtkEntry        *entry;

	/* We can only set the sheet-control-gui once */
	g_return_if_fail (arg_id == ARG_SHEET_CONTROL_GUI);
	g_return_if_fail (item_edit->scg == NULL);

	item_edit->scg = GTK_VALUE_POINTER (*arg);
	sv = ((SheetControl *) item_edit->scg)->view;
	item_edit->entry = wbcg_get_entry (item_edit->scg->wbcg);
	item_edit->pos = sv->edit_pos;

	entry = item_edit->entry;
	item_edit->signal_changed = g_signal_connect (G_OBJECT (gtk_widget_get_parent (
									GTK_WIDGET (entry))),
		"changed",
		G_CALLBACK (entry_changed), item_edit);
	item_edit->signal_key_press = g_signal_connect_after (G_OBJECT (entry),
		"key-press-event",
		G_CALLBACK (entry_event), item_edit);
	item_edit->signal_button_press = g_signal_connect_after (G_OBJECT (entry),
		"notify::cursor-position",
		G_CALLBACK (entry_cursor_event), item_edit);

	scan_for_range (item_edit, "");

	/*
	 * Init the auto-completion
	 */

	/* set the font and the upper left corner if this is the first pass */
	if (item_edit->style_font == NULL) {
		MStyle *mstyle = sheet_style_get (sv->sheet,
			item_edit->pos.col, item_edit->pos.row);
		item_edit->style_font = scg_get_style_font (sv->sheet, mstyle);

		/* move inwards 1 pixel for the grid line */
		item->x1 = 1 + gcanvas->first_offset.col +
			scg_colrow_distance_get (item_edit->scg, TRUE,
					  gcanvas->first.col, item_edit->pos.col);
		item->y1 = 1 + gcanvas->first_offset.row +
			scg_colrow_distance_get (item_edit->scg, FALSE,
					  gcanvas->first.row, item_edit->pos.row);

		item->x2 = item->x1 + 1;
		item->y2 = item->y2 + 1;
	}

	item_edit_cursor_blink_start (item_edit);

	gnome_canvas_item_request_update (item);
}

/*
 * ItemEdit class initialization
 */
static void
item_edit_class_init (ItemEditClass *item_edit_class)
{
	GtkObjectClass  *object_class;
	GnomeCanvasItemClass *item_class;

	item_edit_parent_class = g_type_class_peek (gnome_canvas_item_get_type ());

	object_class = (GtkObjectClass *) item_edit_class;
	item_class = (GnomeCanvasItemClass *) item_edit_class;

	gtk_object_add_arg_type ("ItemEdit::SheetControlGUI", GTK_TYPE_POINTER,
				 GTK_ARG_WRITABLE, ARG_SHEET_CONTROL_GUI);

	object_class->set_arg = item_edit_set_arg;
	object_class->destroy = item_edit_destroy;

	/* GnomeCanvasItem method overrides */
	item_class->update      = item_edit_update;
	item_class->draw        = item_edit_draw;
	item_class->point       = item_edit_point;
	item_class->event       = item_edit_event;
}

GSF_CLASS (ItemEdit, item_edit,
	   item_edit_class_init, item_edit_init,
	   GNOME_TYPE_CANVAS_ITEM);

void
item_edit_disable_highlight (ItemEdit *item_edit)
{
	g_return_if_fail (item_edit != NULL);
	g_return_if_fail (IS_ITEM_EDIT (item_edit));

	entry_destroy_feedback_range (item_edit);
	item_edit->feedback_disabled = TRUE;
}

void
item_edit_enable_highlight (ItemEdit *item_edit)
{
	g_return_if_fail (item_edit != NULL);
	g_return_if_fail (IS_ITEM_EDIT (item_edit));

	item_edit->feedback_disabled = FALSE;
}

/* vim: set sw=8: */
/*
 * item-edit.c : Edit facilities for worksheets.
 *
 * (C) 1999-2002 Miguel de Icaza & Jody Goldberg
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
#include "sheet-view.h"
#include "sheet-style.h"
#include "sheet-merge.h"
#include "value.h"
#include "ranges.h"
#include "style.h"
#include "style-color.h"
#include "pattern.h"
#include "parse-util.h"
#include "workbook.h"
#include "workbook-edit.h"
#include "gui-util.h"
#include "widgets/gnumeric-expr-entry.h"
#include "item-debug.h"
#define GNUMERIC_ITEM "EDIT"

#include <gsf/gsf-impl-utils.h>
#include <gal/widgets/e-cursors.h>
#include <string.h>

static FooCanvasItem *item_edit_parent_class;

/* The arguments we take */
enum {
	ARG_0,
	ARG_SHEET_CONTROL_GUI	/* The SheetControlGUI * argument */
};

static void
entry_destroy_feedback_range (ItemEdit *item_edit)
{
	if (item_edit->feedback_cursor != NULL) {
		gtk_object_destroy (GTK_OBJECT (item_edit->feedback_cursor));
		item_edit->feedback_cursor = NULL;
	}
}

/* WARNING : DO NOT CALL THIS FROM FROM UPDATE.  It may create another
 *           canvas-item which would in turn call update and confuse the
 *           canvas.
 */
static void
scan_for_range (ItemEdit *ie)
{
	Range  range;
	Sheet *sheet = ((SheetControl *) ie->scg)->sheet;
	Sheet *parse_sheet;
	GnmExprEntry *gee = GNM_EXPR_ENTRY (
		gtk_widget_get_parent (GTK_WIDGET (ie->entry)));

	if (!ie->feedback_disabled) {
		gnm_expr_expr_find_range (gee);
		if (gnm_expr_entry_get_rangesel (gee, &range, &parse_sheet) &&
		    (parse_sheet == sheet)) {
			if (ie->feedback_cursor == NULL)
				ie->feedback_cursor = foo_canvas_item_new (
					FOO_CANVAS_GROUP (ie->canvas_item.canvas->root),
					item_cursor_get_type (),
					"SheetControlGUI",	ie->scg,
					"Style",		ITEM_CURSOR_BLOCK,
					"Color",		"blue",
					NULL);
			item_cursor_bound_set (ITEM_CURSOR (ie->feedback_cursor), &range);
			return;
		}
	}

	entry_destroy_feedback_range (ie);
}

static void
item_edit_draw (FooCanvasItem *item, GdkDrawable *drawable,
		GdkEventExpose *expose)
{
	GtkWidget *canvas   = GTK_WIDGET (item->canvas);
	ItemEdit *item_edit = ITEM_EDIT (item);
	SheetControl *sc = (SheetControl *) item_edit->scg;
	ColRowInfo const *ci = sheet_col_get_info (sc->sheet,
						   item_edit->pos.col);
	int const left_pos = ((int)item->x1) + ci->margin_a;
	int top_pos, text_height, height;
	int cursor_pos = gtk_editable_get_position (GTK_EDITABLE (item_edit->entry));
	char const *text, *entered_text;
	PangoLayout	*layout;
	PangoAttrList	*attrs;
	PangoAttribute  *attr;
	PangoRectangle	 pos;
	MStyle const 	*style = item_edit->style;
	StyleFont	*style_font = item_edit->style_font;

	if (style == NULL)
		return;

       	/* Draw the background (recall that gdk_draw_rectangle excludes far coords) */
	gdk_draw_rectangle (
		drawable, item_edit->fill_gc, TRUE,
		(int)item->x1, (int)item->y1,
		(int)(item->x2 - item->x1), (int)(item->y2 - item->y1));

	entered_text = gtk_entry_get_text (item_edit->entry);
	text = wbcg_edit_get_display_text (item_edit->scg->wbcg);
	layout = gtk_widget_create_pango_layout  (GTK_WIDGET (item->canvas), text);

	pango_layout_set_font_description (layout,
		pango_context_get_font_description (style_font->pango.context));
	pango_layout_set_wrap (layout, PANGO_WRAP_CHAR);
	pango_layout_set_width (layout, (int)(item->x2 - item->x1)*PANGO_SCALE);

 	attrs = pango_attr_list_new ();
	pango_layout_set_attributes (layout, attrs);

	{
		StyleColor *fore = mstyle_get_color (style, MSTYLE_COLOR_FORE);
		attr = pango_attr_foreground_new (fore->color.red,
						  fore->color.green,
						  fore->color.blue);
		attr->start_index = 0;
		attr->end_index = -1;
		pango_attr_list_insert (attrs, attr);
	}
	if (entered_text != NULL && entered_text != text) {
		StyleColor *color;
		int const start = strlen (entered_text);

		color = mstyle_get_color (style, MSTYLE_COLOR_FORE);
		attr = pango_attr_background_new (
			color->color.red, color->color.green, color->color.blue);
		attr->start_index = start;
		attr->end_index = G_MAXINT;
		pango_attr_list_insert (attrs, attr);

		color = mstyle_get_color (style, MSTYLE_COLOR_BACK);
		attr = pango_attr_foreground_new (
			color->color.red, color->color.green, color->color.blue);
		attr->start_index = start;
		attr->end_index = G_MAXINT;
		pango_attr_list_insert (attrs, attr);
	}
	/* Handle underlining and strikethrough */
	switch (mstyle_get_font_uline (style)) {
	case UNDERLINE_SINGLE :
		attr = pango_attr_underline_new (PANGO_UNDERLINE_SINGLE);
		attr->start_index = 0;
		attr->end_index = -1;
		pango_attr_list_insert (attrs, attr);
		break;

	case UNDERLINE_DOUBLE :
		attr = pango_attr_underline_new (PANGO_UNDERLINE_DOUBLE);
		attr->start_index = 0;
		attr->end_index = -1;
		pango_attr_list_insert (attrs, attr);
		break;

	default :
		break;
	};
	if (mstyle_get_font_strike (style)){
		attr = pango_attr_strikethrough_new (TRUE);
		attr->start_index = 0;
		attr->end_index = -1;
		pango_attr_list_insert (attrs, attr);
	}
	if (GNM_CANVAS (canvas)->preedit_length) {
		PangoAttrList *tmp_attrs = pango_attr_list_new ();
		pango_attr_list_splice (tmp_attrs, GNM_CANVAS (canvas)->preedit_attrs,
			g_utf8_offset_to_pointer (text, cursor_pos) - text,
			g_utf8_offset_to_pointer (text, cursor_pos + GNM_CANVAS (canvas)->preedit_length) - text);
		pango_layout_set_attributes (layout, tmp_attrs);
		pango_attr_list_unref (tmp_attrs);
	}

	pango_layout_get_pixel_size (layout, NULL, &text_height);
	pango_layout_index_to_pos (layout,
		g_utf8_offset_to_pointer (text, cursor_pos) - text, &pos);

	top_pos = ((int)item->y1) + 1; /* grid line */
	height = (int)(item->y2 - item->y1) - 1;
	switch (mstyle_get_align_v (style)) {
	default:
		g_warning ("Unhandled cell vertical alignment.");

	case VALIGN_JUSTIFY:
	case VALIGN_TOP:
		/*
		 * top_pos == first pixel past margin
		 * add font ascent
		 */
		break;

	case VALIGN_CENTER:
		top_pos += (height - text_height)/2;
		break;

	case VALIGN_BOTTOM:
		/*
		 * rect.y == first pixel past margin
		 * add height == first pixel in lower margin
		 * subtract font descent
		 */
		top_pos += (height - text_height);
		break;
	}
	gdk_draw_layout (drawable, canvas->style->black_gc,
		left_pos, top_pos, layout);
	if (item_edit->cursor_visible)
		gdk_draw_line (drawable, canvas->style->black_gc,
			left_pos + PANGO_PIXELS (pos.x), top_pos + PANGO_PIXELS (pos.y),
			left_pos + PANGO_PIXELS (pos.x), top_pos + PANGO_PIXELS (pos.y + pos.height) - 1);
	g_object_unref (G_OBJECT (layout));
	pango_attr_list_unref (attrs);
}

static double
item_edit_point (FooCanvasItem *item, double c_x, double c_y, int cx, int cy,
		 FooCanvasItem **actual_item)
{
	*actual_item = NULL;
	if ((cx < item->x1) || (cy < item->y1) || (cx >= item->x2) || (cy >= item->y2))
		return 10000.0;

	*actual_item = item;
	return 0.0;
}

static int
item_edit_event (FooCanvasItem *item, GdkEvent *event)
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
recalc_spans (FooCanvasItem *item)
{
	ItemEdit  const *item_edit = ITEM_EDIT (item);
	GnmCanvas const *gcanvas = GNM_CANVAS (item->canvas);
	PangoLayout *layout;
	ColRowInfo const *cri;
	Sheet	   const *sheet  = sc_sheet (SHEET_CONTROL (item_edit->scg));
	StyleFont  const *style_font = item_edit->style_font;
	Range	   const *merged;
	int end_col, end_row, tmp, width, height, col_size;

	end_col = item_edit->pos.col;
	cri = sheet_col_get_info (sheet, end_col);

	g_return_if_fail (cri != NULL);

	layout = pango_layout_copy (gtk_entry_get_layout (item_edit->entry));
	pango_layout_set_font_description (layout,
		pango_context_get_font_description (style_font->pango.context));
	pango_layout_set_wrap (layout, PANGO_WRAP_CHAR);
	pango_layout_set_width (layout, -1);
	pango_layout_get_pixel_size (layout, &width, &height);

	/* Start after the grid line and the left margin */
	col_size = cri->size_pixels - cri->margin_a - 1;
	while (col_size < width &&
	       end_col <= gcanvas->last_full.col &&
	       end_col < SHEET_MAX_COLS-1) {
		end_col++;
		cri = sheet_col_get_info (sheet, end_col);
		g_return_if_fail (cri != NULL);
		if (cri->visible)
			col_size += cri->size_pixels;
	}

	merged = sheet_merge_is_corner (sheet, &item_edit->pos);
	if (merged != NULL) {
		if (end_col < merged->end.col)
			end_col = merged->end.col;
		end_row = merged->end.row;
	} else
		end_row = item_edit->pos.row;

	/* The lower right is based on the span size excluding the grid lines
	 * Recall that the bound excludes the far point */
	item->x2 = 1 + item->x1 +
		scg_colrow_distance_get (item_edit->scg, TRUE,
					 item_edit->pos.col, end_col+1) - 2;

	tmp = gcanvas->first_offset.col +
		GTK_WIDGET (item->canvas)->allocation.width;
	if (item->x2 >= tmp) {
		item->x2 = tmp;
		pango_layout_set_width (layout, (item->x2 - item->x1 + 1)*PANGO_SCALE);
		pango_layout_get_pixel_size (layout, &width, &height);
	}

	tmp = scg_colrow_distance_get (item_edit->scg, FALSE,
				       item_edit->pos.row, end_row+1) - 2;
	item->y2 = 1 + item->y1 + MAX (height, tmp);
	g_object_unref (layout);
}


static void
item_edit_update (FooCanvasItem *item,  double i2w_dx, double i2w_dy, int flags)
{
	ItemEdit *item_edit = ITEM_EDIT (item);

	if (FOO_CANVAS_ITEM_CLASS (item_edit_parent_class)->update)
		(*FOO_CANVAS_ITEM_CLASS(item_edit_parent_class)->update)(item, i2w_dx, i2w_dy, flags);

	/* do not calculate spans until after row/col has been set */
	if (item_edit->style_font != NULL) {
		/* Redraw before and after in case the span changes */
		foo_canvas_request_redraw (FOO_CANVAS_ITEM (item_edit)->canvas,
					     item->x1, item->y1, item->x2, item->y2);
		recalc_spans (item);
		/* Redraw before and after in case the span changes */
		foo_canvas_request_redraw (FOO_CANVAS_ITEM (item_edit)->canvas,
					     (int)item->x1, (int)item->y1, (int)item->x2, (int)item->y2);
	}
}

static void
item_edit_realize (FooCanvasItem *item)
{
	ItemEdit *ie = ITEM_EDIT (item);
	if (FOO_CANVAS_ITEM_CLASS (item_edit_parent_class)->realize)
		(*FOO_CANVAS_ITEM_CLASS (item_edit_parent_class)->realize)(item);

	ie->fill_gc = gdk_gc_new (GTK_WIDGET (item->canvas)->window);
	if (!gnumeric_background_set_gc (ie->style, ie->fill_gc, item->canvas, FALSE))
		gdk_gc_set_foreground (ie->fill_gc, &gs_white);
}

static void
item_edit_unrealize (FooCanvasItem *item)
{
	ItemEdit *ie = ITEM_EDIT (item);
	g_object_unref (G_OBJECT (ie->fill_gc));	ie->fill_gc = NULL;
	if (FOO_CANVAS_ITEM_CLASS (item_edit_parent_class)->unrealize)
		(*FOO_CANVAS_ITEM_CLASS (item_edit_parent_class)->unrealize)(item);
}

static int
cb_item_edit_cursor_blink (ItemEdit *item_edit)
{
	FooCanvasItem *item = FOO_CANVAS_ITEM (item_edit);

	item_edit->cursor_visible = !item_edit->cursor_visible;

	foo_canvas_item_request_update (item);
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
	gboolean blink;
	int	 blink_time;

	g_object_get (gtk_widget_get_settings (
		GTK_WIDGET (item_edit->canvas_item.canvas)),
		"gtk-cursor-blink-time",	&blink_time,
		"gtk-cursor-blink", 	&blink,
		NULL);
	if (blink)
		item_edit->blink_timer = gtk_timeout_add ( blink_time,
			(GtkFunction)&cb_item_edit_cursor_blink,
			item_edit);
}

/*
 * Instance initialization
 */
static void
item_edit_init (ItemEdit *item_edit)
{
	FooCanvasItem *item = FOO_CANVAS_ITEM (item_edit);

	item->x1 = 0;
	item->y1 = 0;
	item->x2 = 1;
	item->y2 = 1;

	item_edit->scg = NULL;
	item_edit->pos.col = -1;
	item_edit->pos.row = -1;
	item_edit->style_font = NULL;
	item_edit->style      = NULL;
	item_edit->cursor_visible = TRUE;
	item_edit->feedback_disabled = FALSE;
	item_edit->feedback_cursor = NULL;
	item_edit->fill_gc = NULL;
}

/*
 * Invoked when the GtkEntry has changed
 *
 * We use this to sync up the GtkEntry with our display on the screen.
 */
static void
entry_changed (GnmExprEntry *ignore, void *data)
{
	FooCanvasItem *item = FOO_CANVAS_ITEM (data);
	ItemEdit *item_edit = ITEM_EDIT (item);
	char const *text = gtk_entry_get_text (item_edit->entry);

	if (gnm_expr_char_start_p (text))
		scan_for_range (item_edit);

	foo_canvas_item_request_update (item);
}

static void
item_edit_destroy (GtkObject *o)
{
	ItemEdit *item_edit = ITEM_EDIT (o);
	GtkEntry *entry = item_edit->entry;

	item_edit_cursor_blink_stop (item_edit);
	entry_destroy_feedback_range (item_edit);

	scg_set_display_cursor (item_edit->scg);

	if (item_edit->style_font != NULL) {
		style_font_unref (item_edit->style_font);
		item_edit->style_font = NULL;
	}
	if (item_edit->style != NULL) {
		mstyle_unref (item_edit->style);
		item_edit->style= NULL;
	}

	if (GTK_OBJECT_CLASS (item_edit_parent_class)->destroy)
		(*GTK_OBJECT_CLASS (item_edit_parent_class)->destroy)(o);
}

static int
entry_event (GtkEntry *entry, GdkEvent *event, FooCanvasItem *item)
{
	entry_changed (NULL, item);
	return TRUE;
}

static int
entry_cursor_event (GtkEntry *entry, GParamSpec *pspec, FooCanvasItem *item)
{
	entry_changed (NULL, item);
	return TRUE;
}

static void
item_edit_set_arg (GtkObject *o, GtkArg *arg, guint arg_id)
{
	FooCanvasItem *item      = FOO_CANVAS_ITEM (o);
	ItemEdit        *item_edit = ITEM_EDIT (o);
	GnmCanvas	*gcanvas   = GNM_CANVAS (item->canvas);
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
	g_signal_connect_object (G_OBJECT (gtk_widget_get_parent (GTK_WIDGET (entry))),
		"changed",
		G_CALLBACK (entry_changed), G_OBJECT (item_edit), 0);
	g_signal_connect_object (G_OBJECT (entry),
		"key-press-event",
		G_CALLBACK (entry_event), G_OBJECT (item_edit), G_CONNECT_AFTER);
	g_signal_connect_object (G_OBJECT (entry),
		"notify::cursor-position",
		G_CALLBACK (entry_cursor_event), G_OBJECT (item_edit), G_CONNECT_AFTER);

	scan_for_range (item_edit);

	/* set the font and the upper left corner if this is the first pass */
	if (item_edit->style_font == NULL) {
		item_edit->style = mstyle_copy (sheet_style_get (sv->sheet,
			item_edit->pos.col, item_edit->pos.row));
		item_edit->style_font = scg_get_style_font (sv->sheet, item_edit->style);

		if (mstyle_get_align_h (item_edit->style) == HALIGN_GENERAL)
			mstyle_set_align_h (item_edit->style, HALIGN_LEFT);

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

	foo_canvas_item_request_update (item);
}

/*
 * ItemEdit class initialization
 */
static void
item_edit_class_init (ItemEditClass *item_edit_class)
{
	GtkObjectClass  *object_class;
	FooCanvasItemClass *item_class;

	item_edit_parent_class = g_type_class_peek_parent (item_edit_class);

	object_class = (GtkObjectClass *) item_edit_class;
	item_class = (FooCanvasItemClass *) item_edit_class;

	gtk_object_add_arg_type ("ItemEdit::SheetControlGUI", GTK_TYPE_POINTER,
				 GTK_ARG_WRITABLE, ARG_SHEET_CONTROL_GUI);

	object_class->set_arg = item_edit_set_arg;
	object_class->destroy = item_edit_destroy;

	/* FooCanvasItem method overrides */
	item_class->update      = item_edit_update;
	item_class->realize     = item_edit_realize;
	item_class->unrealize   = item_edit_unrealize;
	item_class->draw        = item_edit_draw;
	item_class->point       = item_edit_point;
	item_class->event       = item_edit_event;
}

GSF_CLASS (ItemEdit, item_edit,
	   item_edit_class_init, item_edit_init,
	   FOO_TYPE_CANVAS_ITEM);

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

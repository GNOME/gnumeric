/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * item-edit.c : Edit facilities for worksheets.
 *
 * (C) 1999-2004 Miguel de Icaza & Jody Goldberg
 *
 * This module provides:
 *   * Integration of an in-sheet text editor (GtkEntry) with the Workbook
 *     GtkEntry as a canvas item.
 *
 *   * Feedback on expressions in the spreadsheet (referenced cells or
 *     ranges are highlighted on the spreadsheet).
 */
#include <gnumeric-config.h>
#include <glib/gi18n.h>
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

#include <gtk/gtkentry.h>
#include <libfoocanvas/foo-canvas.h>
#include <gsf/gsf-impl-utils.h>
#include <string.h>

static FooCanvasItemClass *parent_class;

struct _ItemEdit {
	FooCanvasItem canvas_item;

	SheetControlGUI *scg;
	GtkEntry   	*entry;		/* Utility pointer to the workbook entry */

	PangoLayout	*layout;

	/* Where are we */
	GnmCellPos pos;
	gboolean   cursor_visible;
	int        blink_timer;

	GnmFont   *gfont;
	GnmStyle  *style;
	GdkGC     *fill_gc;	/* Default background fill gc */

	/* When editing, if the cursor is inside a cell name, or a cell range,
	 * we highlight this on the spreadsheet. */
	FooCanvasItem *feedback_cursor [SCG_NUM_PANES];
	gboolean       feedback_disabled;
};

typedef FooCanvasItemClass ItemEditClass;

/* The arguments we take */
enum {
	ARG_0,
	ARG_SHEET_CONTROL_GUI	/* The SheetControlGUI * argument */
};

static void
ie_destroy_feedback_range (ItemEdit *ie)
{
	int i = G_N_ELEMENTS (ie->feedback_cursor);

	while (i-- > 0)
		if (ie->feedback_cursor[i] != NULL) {
			gtk_object_destroy (GTK_OBJECT (ie->feedback_cursor[i]));
			ie->feedback_cursor[i] = NULL;
		}
}

/* WARNING : DO NOT CALL THIS FROM FROM UPDATE.  It may create another
 *           canvas-item which would in turn call update and confuse the
 *           canvas.
 */
static void
ie_scan_for_range (ItemEdit *ie)
{
	GnmRange  range;
	Sheet *sheet = sc_sheet (SHEET_CONTROL (ie->scg));
	Sheet *parse_sheet;
	GnmExprEntry *gee = GNM_EXPR_ENTRY (
		gtk_widget_get_parent (GTK_WIDGET (ie->entry)));

	if (!ie->feedback_disabled) {
		gnm_expr_expr_find_range (gee);
		if (gnm_expr_entry_get_rangesel (gee, &range, &parse_sheet) &&
		    parse_sheet == sheet) {
			SCG_FOREACH_PANE (ie->scg, pane, {
				if (ie->feedback_cursor[i] == NULL)
					ie->feedback_cursor[i] = foo_canvas_item_new (
						FOO_CANVAS_GROUP (FOO_CANVAS (pane->gcanvas)->root),
					item_cursor_get_type (),
					"SheetControlGUI",	ie->scg,
					"style",		ITEM_CURSOR_BLOCK,
					"color",		"blue",
					NULL);
				item_cursor_bound_set (ITEM_CURSOR (ie->feedback_cursor[i]), &range);
			});
			return;
		}
	}

	ie_destroy_feedback_range (ie);
}

static void
item_edit_draw (FooCanvasItem *item, GdkDrawable *drawable,
		GdkEventExpose *expose)
{
	GtkWidget *canvas   = GTK_WIDGET (item->canvas);
	ItemEdit *ie = ITEM_EDIT (item);
	Sheet	   const *sheet  = sc_sheet (SHEET_CONTROL (ie->scg));
	ColRowInfo const *ci = sheet_col_get_info (sheet, ie->pos.col);
	int const left_pos = ((int)item->x1) + ci->margin_a;
	int top_pos, text_height, height;
	int cursor_pos   = gtk_editable_get_position (GTK_EDITABLE (ie->entry));
	char const *text = gtk_entry_get_text (ie->entry);
	PangoRectangle	 pos;

	if (ie->style == NULL)
		return;

       	/* Draw the background (recall that gdk_draw_rectangle excludes far coords) */
	gdk_draw_rectangle (
		drawable, ie->fill_gc, TRUE,
		(int)item->x1, (int)item->y1,
		(int)(item->x2 - item->x1), (int)(item->y2 - item->y1));

	pango_layout_get_pixel_size (ie->layout, NULL, &text_height);
	pango_layout_index_to_pos (ie->layout,
		g_utf8_offset_to_pointer (text, cursor_pos) - text, &pos);

	top_pos = ((int)item->y1) + 1; /* grid line */
	height = (int)(item->y2 - item->y1) - 1;
	switch (mstyle_get_align_v (ie->style)) {
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
		left_pos, top_pos, ie->layout);
	if (ie->cursor_visible)
		gdk_draw_line (drawable, canvas->style->black_gc,
			left_pos + PANGO_PIXELS (pos.x), top_pos + PANGO_PIXELS (pos.y),
			left_pos + PANGO_PIXELS (pos.x), top_pos + PANGO_PIXELS (pos.y + pos.height) - 1);
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
		gnm_widget_set_cursor_type (GTK_WIDGET (item->canvas), GDK_XTERM);
		return TRUE;
	default :
		break;
	}
	return FALSE;
}

static void
ie_layout (FooCanvasItem *item)
{
	ItemEdit *ie = ITEM_EDIT (item);
	GtkWidget const  *canvas = GTK_WIDGET (item->canvas);
	GnmCanvas const  *gcanvas = GNM_CANVAS (item->canvas);
	ColRowInfo const *cri;
	Sheet	   const *sheet  = sc_sheet (SHEET_CONTROL (ie->scg));
	GnmFont  const *gfont = ie->gfont;
	GnmRange	   const *merged;
	int end_col, end_row, tmp, width, height, col_size;
	char const *text, *entered_text;
	PangoAttrList	*attrs;
	PangoAttribute  *attr;
	int cursor_pos = gtk_editable_get_position (GTK_EDITABLE (ie->entry));

	end_col = ie->pos.col;
	cri = sheet_col_get_info (sheet, end_col);

	g_return_if_fail (cri != NULL);

	entered_text = gtk_entry_get_text (ie->entry);
	text = wbcg_edit_get_display_text (scg_get_wbcg (ie->scg));
	pango_layout_set_text (ie->layout, text, -1);

	pango_layout_set_font_description (ie->layout, gfont->pango.font_descr);
	pango_layout_set_wrap (ie->layout, PANGO_WRAP_CHAR);
	pango_layout_set_width (ie->layout, (int)(item->x2 - item->x1)*PANGO_SCALE);

	attrs = wbcg_edit_get_markup (scg_get_wbcg (ie->scg));
	if (attrs != NULL)
		attrs = pango_attr_list_copy (attrs);
	else
		attrs = mstyle_generate_attrs_full (ie->style);

	/* reverse video the auto completion text  */
	if (entered_text != NULL && entered_text != text) {
		GnmColor *color;
		int const start = strlen (entered_text);

		color = mstyle_get_color (ie->style, MSTYLE_COLOR_FORE);
		attr = pango_attr_background_new (
			color->color.red, color->color.green, color->color.blue);
		attr->start_index = start;
		attr->end_index = G_MAXINT;
		pango_attr_list_insert (attrs, attr);

		color = mstyle_get_color (ie->style, MSTYLE_COLOR_BACK);
		attr = pango_attr_foreground_new (
			color->color.red, color->color.green, color->color.blue);
		attr->start_index = start;
		attr->end_index = G_MAXINT;
		pango_attr_list_insert (attrs, attr);
	}
	pango_layout_set_attributes (ie->layout, attrs);
	pango_attr_list_unref (attrs);

	text = wbcg_edit_get_display_text (scg_get_wbcg (ie->scg));

	if (GNM_CANVAS (canvas)->preedit_length) {
		PangoAttrList *tmp_attrs = pango_attr_list_new ();
		pango_attr_list_splice (tmp_attrs, GNM_CANVAS (canvas)->preedit_attrs,
			g_utf8_offset_to_pointer (text, cursor_pos) - text,
			g_utf8_offset_to_pointer (text, cursor_pos + GNM_CANVAS (canvas)->preedit_length) - text);
		pango_layout_set_attributes (ie->layout, tmp_attrs);
		pango_attr_list_unref (tmp_attrs);
	}

	pango_layout_set_width (ie->layout, -1);
	pango_layout_get_pixel_size (ie->layout, &width, &height);

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

	merged = sheet_merge_is_corner (sheet, &ie->pos);
	if (merged != NULL) {
		if (end_col < merged->end.col)
			end_col = merged->end.col;
		end_row = merged->end.row;
	} else
		end_row = ie->pos.row;

	/* The lower right is based on the span size excluding the grid lines
	 * Recall that the bound excludes the far point */
	item->x2 = 1 + item->x1 +
		scg_colrow_distance_get (ie->scg, TRUE,
					 ie->pos.col, end_col+1) - 2;

	tmp = gcanvas->first_offset.col +
		GTK_WIDGET (item->canvas)->allocation.width;
	if (item->x2 >= tmp) {
		item->x2 = tmp;
		pango_layout_set_width (ie->layout, (item->x2 - item->x1 + 1)*PANGO_SCALE);
		pango_layout_get_pixel_size (ie->layout, &width, &height);
	}

	tmp = scg_colrow_distance_get (ie->scg, FALSE,
				       ie->pos.row, end_row+1) - 2;
	item->y2 = item->y1 + MAX (height, tmp);
}


static void
item_edit_update (FooCanvasItem *item,  double i2w_dx, double i2w_dy, int flags)
{
	ItemEdit *ie = ITEM_EDIT (item);

	if (parent_class->update)
		(parent_class->update)(item, i2w_dx, i2w_dy, flags);

	/* do not calculate spans until after row/col has been set */
	if (ie->gfont != NULL) {
		/* Redraw before and after in case the span changes */
		foo_canvas_item_request_redraw (item);
		ie_layout (item);
		foo_canvas_item_request_redraw (item);
	}
}

static void
item_edit_realize (FooCanvasItem *item)
{
	ItemEdit *ie = ITEM_EDIT (item);
	if (parent_class->realize)
		(parent_class->realize) (item);

	ie->fill_gc = gdk_gc_new (GTK_WIDGET (item->canvas)->window);
	if (!gnumeric_background_set_gc (ie->style, ie->fill_gc, item->canvas, FALSE))
		gdk_gc_set_rgb_fg_color (ie->fill_gc, &gs_white);

	ie->layout = gtk_widget_create_pango_layout (GTK_WIDGET (item->canvas), NULL);
}

static void
item_edit_unrealize (FooCanvasItem *item)
{
	ItemEdit *ie = ITEM_EDIT (item);

	g_object_unref (G_OBJECT (ie->fill_gc));
	ie->fill_gc = NULL;

	g_object_unref (G_OBJECT (ie->layout));
	ie->layout = NULL;

	if (parent_class->unrealize)
		(parent_class->unrealize) (item);
}

static int
cb_item_edit_cursor_blink (ItemEdit *ie)
{
	FooCanvasItem *item = FOO_CANVAS_ITEM (ie);

	ie->cursor_visible = !ie->cursor_visible;

	foo_canvas_item_request_update (item);
	return TRUE;
}

static void
item_edit_cursor_blink_stop (ItemEdit *ie)
{
	if (ie->blink_timer != -1) {
		g_source_remove (ie->blink_timer);
		ie->blink_timer = -1;
	}
}

static void
item_edit_cursor_blink_start (ItemEdit *ie)
{
	gboolean blink;
	int	 blink_time;

	g_object_get (gtk_widget_get_settings (
		GTK_WIDGET (ie->canvas_item.canvas)),
		"gtk-cursor-blink-time",	&blink_time,
		"gtk-cursor-blink", 		&blink,
		NULL);
	if (blink)
		ie->blink_timer = g_timeout_add ( blink_time,
			(GSourceFunc) cb_item_edit_cursor_blink, ie);
}

/*
 * Instance initialization
 */
static void
item_edit_init (ItemEdit *ie)
{
	FooCanvasItem *item = FOO_CANVAS_ITEM (ie);
	int i;

	item->x1 = 0;
	item->y1 = 0;
	item->x2 = 1;
	item->y2 = 1;

	ie->scg = NULL;
	ie->pos.col = -1;
	ie->pos.row = -1;
	ie->gfont = NULL;
	ie->style      = NULL;
	ie->cursor_visible = TRUE;
	ie->feedback_disabled = FALSE;
	ie->fill_gc = NULL;
	for (i = G_N_ELEMENTS (ie->feedback_cursor); i-- > 0 ; )
		ie->feedback_cursor[i] = NULL;
}

/*
 * Invoked when the GtkEntry has changed
 *
 * We use this to sync up the GtkEntry with our display on the screen.
 */
static void
entry_changed (FooCanvasItem *item)
{
	ItemEdit *ie = ITEM_EDIT (item);
	char const *text = gtk_entry_get_text (ie->entry);

	if (gnm_expr_char_start_p (text))
		ie_scan_for_range (ie);

	foo_canvas_item_request_update (item);
}

static void
item_edit_finalize (GObject *gobject)
{
	ItemEdit *ie = ITEM_EDIT (gobject);

	item_edit_cursor_blink_stop (ie);
	ie_destroy_feedback_range (ie);

	scg_set_display_cursor (ie->scg);

	if (ie->gfont != NULL) {
		style_font_unref (ie->gfont);
		ie->gfont = NULL;
	}
	if (ie->style != NULL) {
		mstyle_unref (ie->style);
		ie->style= NULL;
	}

	(G_OBJECT_CLASS (parent_class)->finalize) (gobject);
}

static int
entry_key_press (FooCanvasItem *item)
{
	entry_changed (item);
	return TRUE;
}

static int
entry_cursor_event (FooCanvasItem *item)
{
	entry_changed (item);
	return TRUE;
}

static void
item_edit_set_property (GObject *gobject, guint param_id,
			GValue const *value, GParamSpec *pspec)
{
	FooCanvasItem *item      = FOO_CANVAS_ITEM (gobject);
	ItemEdit        *ie = ITEM_EDIT (gobject);
	GnmCanvas	*gcanvas   = GNM_CANVAS (item->canvas);
	SheetView const	*sv;
	GtkEntry        *entry;

	/* We can only set the sheet-control-gui once */
	g_return_if_fail (param_id == ARG_SHEET_CONTROL_GUI);
	g_return_if_fail (ie->scg == NULL);

	ie->scg = SHEET_CONTROL_GUI (g_value_get_object (value));

	sv = sc_view (SHEET_CONTROL (ie->scg));
	ie->pos = sv->edit_pos;
	ie->entry = entry = wbcg_get_entry (scg_get_wbcg (ie->scg));
	g_signal_connect_object (G_OBJECT (scg_get_wbcg (ie->scg)),
		"markup-changed",
		G_CALLBACK (foo_canvas_item_request_update), G_OBJECT (ie), G_CONNECT_SWAPPED);
	g_signal_connect_object (G_OBJECT (gtk_widget_get_parent (GTK_WIDGET (entry))),
		"changed",
		G_CALLBACK (entry_changed), G_OBJECT (ie), G_CONNECT_SWAPPED);
	g_signal_connect_object (G_OBJECT (entry),
		"key-press-event",
		G_CALLBACK (entry_key_press), G_OBJECT (ie), G_CONNECT_AFTER|G_CONNECT_SWAPPED);
	g_signal_connect_object (G_OBJECT (entry),
		"notify::cursor-position",
		G_CALLBACK (entry_cursor_event), G_OBJECT (ie), G_CONNECT_AFTER|G_CONNECT_SWAPPED);

	ie_scan_for_range (ie);

	/* set the font and the upper left corner if this is the first pass */
	if (ie->gfont == NULL) {
		ie->style = mstyle_copy (sheet_style_get (sv->sheet,
			ie->pos.col, ie->pos.row));
		ie->gfont = scg_get_style_font
			(sv->sheet->context, sv->sheet, ie->style);

		if (mstyle_get_align_h (ie->style) == HALIGN_GENERAL)
			mstyle_set_align_h (ie->style, HALIGN_LEFT);

		/* move inwards 1 pixel for the grid line */
		item->x1 = 1 + gcanvas->first_offset.col +
			scg_colrow_distance_get (ie->scg, TRUE,
					  gcanvas->first.col, ie->pos.col);
		item->y1 = 1 + gcanvas->first_offset.row +
			scg_colrow_distance_get (ie->scg, FALSE,
					  gcanvas->first.row, ie->pos.row);

		item->x2 = item->x1 + 1;
		item->y2 = item->y2 + 1;
	}

	item_edit_cursor_blink_start (ie);

	foo_canvas_item_request_update (item);
}

/*
 * ItemEdit class initialization
 */
static void
item_edit_class_init (GObjectClass *gobject_class)
{
	FooCanvasItemClass *item_class = (FooCanvasItemClass *) gobject_class;

	parent_class = g_type_class_peek_parent (gobject_class);

	gobject_class->set_property = item_edit_set_property;
	gobject_class->finalize	   = item_edit_finalize;

	g_object_class_install_property (gobject_class, ARG_SHEET_CONTROL_GUI,
		g_param_spec_object ("SheetControlGUI", "SheetControlGUI",
			"the sheet control gui controlling the item",
			SHEET_CONTROL_GUI_TYPE, G_PARAM_WRITABLE));

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
item_edit_disable_highlight (ItemEdit *ie)
{
	g_return_if_fail (ITEM_EDIT (ie) != NULL);
	ie_destroy_feedback_range (ie);
	ie->feedback_disabled = TRUE;
}

void
item_edit_enable_highlight (ItemEdit *ie)
{
	g_return_if_fail (ITEM_EDIT (ie) != NULL);
	ie->feedback_disabled = FALSE;
}

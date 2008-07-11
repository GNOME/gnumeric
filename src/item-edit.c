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
#include <glib/gi18n-lib.h>
#include "gnumeric.h"
#include "item-edit.h"
#include "gnm-pane-impl.h"

#include "item-cursor.h"
#include "sheet-control-gui-priv.h"
#include "sheet.h"
#include "sheet-view.h"
#include "sheet-style.h"
#include "sheet-merge.h"
#include "value.h"
#include "ranges.h"
#include "style.h"
#include "style-font.h"
#include "style-color.h"
#include "pattern.h"
#include "parse-util.h"
#include "workbook.h"
#include "wbc-gtk.h"
#include "gui-util.h"
#include "widgets/gnumeric-expr-entry.h"
#include "item-debug.h"
#define GNUMERIC_ITEM "EDIT"

#include <gtk/gtkentry.h>
#include <goffice/cut-n-paste/foocanvas/foo-canvas.h>
#include <gsf/gsf-impl-utils.h>
#include <string.h>
#include <goffice/utils/go-font.h>

static FooCanvasItemClass *parent_class;

struct _ItemEdit {
	FooCanvasItem item;

	SheetControlGUI *scg;
	GtkEntry	*entry;		/* Utility pointer to the workbook entry */

	PangoLayout	*layout;

	/* Where are we */
	GnmCellPos pos;
	gboolean   cursor_visible;
	int        blink_timer;

	GnmFont   *gfont;
	GnmStyle  *style;
	GdkGC     *fill_gc;	/* Default background fill gc */
};

typedef FooCanvasItemClass ItemEditClass;

/* The arguments we take */
enum {
	ARG_0,
	ARG_SHEET_CONTROL_GUI	/* The SheetControlGUI * argument */
};

static void
get_top_left (ItemEdit const *ie, int *top, int *left)
{
	GnmVAlign const align = gnm_style_get_align_v (ie->style);

	*left = ((int)ie->item.x1) + GNM_COL_MARGIN;
	*top  = (int)ie->item.y1;

	if (align == VALIGN_CENTER || align == VALIGN_DISTRIBUTED ||
	    align == VALIGN_BOTTOM) {
		int text_height, height = (int)(ie->item.y2 - ie->item.y1);
		pango_layout_get_pixel_size (ie->layout, NULL, &text_height);
		*top += (align != VALIGN_BOTTOM)
			? (height - text_height)/2
			: (height - text_height);
	}
}

static void
item_edit_draw (FooCanvasItem *item, GdkDrawable *drawable,
		GdkEventExpose *expose)
{
	ItemEdit  const *ie	= ITEM_EDIT (item);
	GdkGC *black_gc		= GTK_WIDGET (item->canvas)->style->black_gc;
	int top, left;

	if (ie->style == NULL)
		return;

	/* Draw the background (recall that gdk_draw_rectangle excludes far coords) */
	gdk_draw_rectangle (drawable, ie->fill_gc, TRUE,
		(int)item->x1,			(int)item->y1,
		(int)(item->x2 - item->x1),	(int)(item->y2 - item->y1));

	get_top_left (ie, &top, &left);
	gdk_draw_layout (drawable, black_gc, left, top, ie->layout);
	if (ie->cursor_visible) {
		PangoRectangle pos;
		char const *text = gtk_entry_get_text (ie->entry);
		int cursor_pos = gtk_editable_get_position (GTK_EDITABLE (ie->entry));
		pango_layout_index_to_pos (ie->layout,
			g_utf8_offset_to_pointer (text, cursor_pos) - text, &pos);
		gdk_draw_line (drawable, black_gc,
			left + PANGO_PIXELS (pos.x), top + PANGO_PIXELS (pos.y),
			left + PANGO_PIXELS (pos.x), top + PANGO_PIXELS (pos.y + pos.height) - 1);
	}
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
	switch (event->type){
	case GDK_ENTER_NOTIFY:
		gnm_widget_set_cursor_type (GTK_WIDGET (item->canvas), GDK_XTERM);
		return TRUE;

	case GDK_BUTTON_PRESS:
		if (event->button.button == 1) {
			ItemEdit *ie = ITEM_EDIT (item);
			GtkEditable *ed = GTK_EDITABLE (ie->entry);
			int x, y, target_index, trailing, top, left;

			foo_canvas_w2c (item->canvas,
				event->button.x, event->button.y, &x, &y);

			get_top_left (ie, &top, &left);
			y -= top;
			x -= left;

			if (pango_layout_xy_to_index (ie->layout,
						      x * PANGO_SCALE, y * PANGO_SCALE,
						      &target_index, &trailing)) {
				int preedit = GNM_PANE (item->canvas)->preedit_length;
				char const *text = pango_layout_get_text (ie->layout);
				gint cur_index = gtk_editable_get_position (ed);
				cur_index = g_utf8_offset_to_pointer (text, cur_index) - text;

				if (target_index >= cur_index && preedit > 0) {
					if (target_index < (cur_index + preedit)) {
						target_index = cur_index;
						trailing = 0;
					} else
						target_index -= preedit;
				}
				gtk_editable_set_position (GTK_EDITABLE (ie->entry),
					g_utf8_pointer_to_offset (text, text + target_index)
					+ trailing);

				return TRUE;
			}
		}
		break;

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
	GnmPane	 const  *pane = GNM_PANE (item->canvas);
	ColRowInfo const *ci;
	Sheet const	 *sheet  = scg_sheet (ie->scg);
	GnmFont  const   *gfont = ie->gfont;
	GnmRange const   *merged;
	int col, tmp, width, height, col_size;
	char const *text, *entered_text;
	PangoAttrList	*attrs;
	PangoAttribute  *attr;
	int cursor_pos = gtk_editable_get_position (GTK_EDITABLE (ie->entry));

	entered_text = gtk_entry_get_text (ie->entry);
	text = wbcg_edit_get_display_text (scg_wbcg (ie->scg));
	pango_layout_set_text (ie->layout, text, -1);

	pango_layout_set_font_description (ie->layout, gfont->go.font->desc);
	pango_layout_set_wrap (ie->layout, PANGO_WRAP_WORD_CHAR);
	pango_layout_set_width (ie->layout, (int)(item->x2 - item->x1)*PANGO_SCALE);

	attrs = wbcg_edit_get_markup (scg_wbcg (ie->scg), TRUE);
	if (attrs != NULL)
		attrs = pango_attr_list_copy (attrs);
	else
		attrs = gnm_style_generate_attrs_full (ie->style);

	/* reverse video the auto completion text  */
	if (entered_text != NULL && entered_text != text) {
		int const start = strlen (entered_text);
		GnmColor const *color = gnm_style_get_font_color (ie->style);
		attr = pango_attr_background_new (
			color->gdk_color.red, color->gdk_color.green, color->gdk_color.blue);
		attr->start_index = start;
		attr->end_index = G_MAXINT;
		pango_attr_list_insert (attrs, attr);

		color = gnm_style_get_back_color (ie->style);
		attr = pango_attr_foreground_new (
			color->gdk_color.red, color->gdk_color.green, color->gdk_color.blue);
		attr->start_index = start;
		attr->end_index = G_MAXINT;
		pango_attr_list_insert (attrs, attr);
	}
	attr = pango_attr_scale_new (item->canvas->pixels_per_unit);
	attr->start_index = 0;
	attr->end_index = G_MAXINT;
	pango_attr_list_insert_before (attrs, attr);

	pango_layout_set_attributes (ie->layout, attrs);
	pango_attr_list_unref (attrs);

	text = wbcg_edit_get_display_text (scg_wbcg (ie->scg));

	if (pane->preedit_length) {
		PangoAttrList *tmp_attrs = pango_attr_list_new ();
		pango_attr_list_splice (tmp_attrs, pane->preedit_attrs,
			g_utf8_offset_to_pointer (text, cursor_pos) - text,
			g_utf8_offset_to_pointer (text, cursor_pos + pane->preedit_length) - text);
		pango_layout_set_attributes (ie->layout, tmp_attrs);
		pango_attr_list_unref (tmp_attrs);
	}

	pango_layout_set_width (ie->layout, -1);
	pango_layout_get_pixel_size (ie->layout, &width, &height);

	col = ie->pos.col;
	if (NULL == (merged = gnm_sheet_merge_is_corner (sheet, &ie->pos))) {
		ci = sheet_col_get_info (sheet, col);
		g_return_if_fail (ci != NULL);
		col_size = ci->size_pixels;
	} else
		col_size = scg_colrow_distance_get (ie->scg, TRUE,
			merged->start.col, merged->end.col+1);

	/* both margins and the gridline */
	col_size -= GNM_COL_MARGIN + GNM_COL_MARGIN + 1;

	/* far corner based on the span size
	 *	- margin on each end
	 *	- the bound excludes the far point => +1 */
	if (sheet->text_is_rtl) {
		while (col_size < width && col >= pane->first.col) {
			ci = sheet_col_get_info (sheet, col--);

			g_return_if_fail (ci != NULL);

			if (ci->visible)
				col_size += ci->size_pixels;
		}

		tmp = gnm_foo_canvas_x_w2c (item->canvas, pane->first_offset.col);
	} else {
		if (merged != NULL)
			col = merged->end.col;

		while (col_size < width &&
		       col <= pane->last_full.col &&
		       col < gnm_sheet_get_max_cols (sheet)-1) {
			ci = sheet_col_get_info (sheet, ++col);

			g_return_if_fail (ci != NULL);

			if (ci->visible)
				col_size += ci->size_pixels;
		}
		tmp = pane->first_offset.col + canvas->allocation.width;
	}
	item->x2 = item->x1 + col_size + GNM_COL_MARGIN + GNM_COL_MARGIN + 1;

	if (item->x2 >= tmp) {
		item->x2 = tmp;
		pango_layout_set_width (ie->layout, (item->x2 - item->x1 + 1)*PANGO_SCALE);
		pango_layout_get_pixel_size (ie->layout, &width, &height);
	}

	tmp = scg_colrow_distance_get (ie->scg, FALSE, ie->pos.row,
		(merged ? merged->end.row : ie->pos.row) + 1) - 2 + 1;
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
		gdk_gc_set_rgb_fg_color (ie->fill_gc, &gs_yellow);

	ie->layout = gtk_widget_create_pango_layout (GTK_WIDGET (item->canvas), NULL);
	pango_layout_set_alignment (ie->layout,
		scg_sheet (ie->scg)->text_is_rtl ? PANGO_ALIGN_RIGHT : PANGO_ALIGN_LEFT);
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

	foo_canvas_item_request_redraw (item);
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
		GTK_WIDGET (ie->item.canvas)),
		"gtk-cursor-blink-time",	&blink_time,
		"gtk-cursor-blink",		&blink,
		NULL);
	if (blink)
		ie->blink_timer = g_timeout_add ( blink_time,
			(GSourceFunc) cb_item_edit_cursor_blink, ie);
}

static void
item_edit_init (ItemEdit *ie)
{
	FooCanvasItem *item = FOO_CANVAS_ITEM (ie);

	/* Apply an arbitrary size/pos for now.  Init when we get the scg */
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
	ie->fill_gc = NULL;
}

static void
item_edit_dispose (GObject *gobject)
{
	ItemEdit *ie = ITEM_EDIT (gobject);

	item_edit_cursor_blink_stop (ie);

	/* to destroy the feedback ranges */
	SCG_FOREACH_PANE (ie->scg, pane,
		gnm_pane_expr_cursor_stop (pane););

	if (ie->gfont != NULL) {
		gnm_font_unref (ie->gfont);
		ie->gfont = NULL;
	}
	if (ie->style != NULL) {
		gnm_style_unref (ie->style);
		ie->style= NULL;
	}

	G_OBJECT_CLASS (parent_class)->dispose (gobject);
}

static int
cb_entry_key_press (FooCanvasItem *item)
{
	foo_canvas_item_request_update (item);
	return TRUE;
}

static int
cb_entry_cursor_event (FooCanvasItem *item)
{
	/* ensure we draw a cursor when moving quickly no matter what the
	 * current state is */
	ITEM_EDIT (item)->cursor_visible = TRUE;
	foo_canvas_item_request_update (item);
	return TRUE;
}

static void
item_edit_set_property (GObject *gobject, guint param_id,
			GValue const *value, GParamSpec *pspec)
{
	FooCanvasItem *item      = FOO_CANVAS_ITEM (gobject);
	ItemEdit        *ie = ITEM_EDIT (gobject);
	GnmPane		*pane = GNM_PANE (item->canvas);
	SheetView const	*sv;

	/* We can only set the sheet-control-gui once */
	g_return_if_fail (param_id == ARG_SHEET_CONTROL_GUI);
	g_return_if_fail (ie->scg == NULL);

	ie->scg = SHEET_CONTROL_GUI (g_value_get_object (value));

	sv = scg_view (ie->scg);
	ie->pos = sv->edit_pos;
	ie->entry = wbcg_get_entry (scg_wbcg (ie->scg));
	g_signal_connect_object (G_OBJECT (scg_wbcg (ie->scg)),
		"markup-changed",
		G_CALLBACK (foo_canvas_item_request_update), G_OBJECT (ie), G_CONNECT_SWAPPED);
	g_signal_connect_object (G_OBJECT (gtk_widget_get_parent (GTK_WIDGET (ie->entry))),
		"changed",
		G_CALLBACK (foo_canvas_item_request_update), G_OBJECT (ie), G_CONNECT_SWAPPED);
	g_signal_connect_object (G_OBJECT (ie->entry),
		"key-press-event",
		G_CALLBACK (cb_entry_key_press), G_OBJECT (ie), G_CONNECT_AFTER|G_CONNECT_SWAPPED);
	g_signal_connect_object (G_OBJECT (ie->entry),
		"notify::cursor-position",
		G_CALLBACK (cb_entry_cursor_event), G_OBJECT (ie), G_CONNECT_AFTER|G_CONNECT_SWAPPED);

	/* set the font and the upper left corner if this is the first pass */
	if (ie->gfont == NULL) {
		Sheet const *sheet = sv->sheet;
		ie->style = gnm_style_dup (
			sheet_style_get (sheet, ie->pos.col, ie->pos.row));
		ie->gfont = gnm_style_get_font (ie->style,
			sheet->context, sheet->last_zoom_factor_used);
		gnm_font_ref (ie->gfont);

		if (gnm_style_get_align_h (ie->style) == HALIGN_GENERAL)
			gnm_style_set_align_h (ie->style, HALIGN_LEFT);

		/* move inwards 1 pixel from the grid line */
		item->y1 = 1 + pane->first_offset.row +
			scg_colrow_distance_get (ie->scg, FALSE,
				pane->first.row, ie->pos.row);
		item->x1 = 1 + pane->first_offset.col +
			scg_colrow_distance_get (ie->scg, TRUE,
				pane->first.col, ie->pos.col);
		if (sv_sheet (sv)->text_is_rtl) {
			GnmRange const *merged = gnm_sheet_merge_is_corner (sheet, &ie->pos);
			int end_col = ie->pos.col;
			if (merged != NULL)
				end_col = merged->end.col;

			/* -1 to remove the above, then 2 more to move from next cell back */
			item->x1 = 1 + gnm_foo_canvas_x_w2c (item->canvas, item->x1 +
				scg_colrow_distance_get (ie->scg, TRUE,
					ie->pos.col, end_col + 1) - 1);
		}

		item->x2 = item->x1 + 1;
		item->y2 = item->y2 + 1;
	}

	item_edit_cursor_blink_start (ie);

	foo_canvas_item_request_update (item);
}

static void
item_edit_class_init (GObjectClass *gobject_class)
{
	FooCanvasItemClass *item_class = (FooCanvasItemClass *) gobject_class;

	parent_class = g_type_class_peek_parent (gobject_class);

	gobject_class->set_property = item_edit_set_property;
	gobject_class->dispose	   = item_edit_dispose;

	g_object_class_install_property (gobject_class, ARG_SHEET_CONTROL_GUI,
		g_param_spec_object ("SheetControlGUI", "SheetControlGUI",
			"the sheet control gui controlling the item",
			SHEET_CONTROL_GUI_TYPE,
			/* resist the urge to use G_PARAM_CONSTRUCT_ONLY
			 * We are going through foo_canvas_item_new, which
			 * calls g_object_new assigns the parent pointer before
			 * setting the construction parameters */
			 GSF_PARAM_STATIC | G_PARAM_WRITABLE));

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

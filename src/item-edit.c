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
#define GNUMERIC_ITEM "EDIT"

#include <gtk/gtk.h>
#include <gsf/gsf-impl-utils.h>
#include <string.h>
#include <goffice/goffice.h>

static GocItemClass *parent_class;

struct _ItemEdit {
	GocItem item;

	SheetControlGUI *scg;
	GtkEntry	*entry;		/* Utility pointer to the workbook entry */

	PangoLayout	*layout;

	/* Where are we */
	GnmCellPos pos;
	gboolean   cursor_visible;
	int        blink_timer;

	GnmFont   *gfont;
	GnmStyle  *style;
};

typedef GocItemClass ItemEditClass;

/* The arguments we take */
enum {
	ARG_0,
	ARG_SHEET_CONTROL_GUI	/* The SheetControlGUI * argument */
};

static void
get_top_left (ItemEdit const *ie, int *top, int *left)
{
	GnmVAlign const align = gnm_style_get_align_v (ie->style);
	GocItem *item = GOC_ITEM (ie);
	GocCanvas *canvas = item->canvas;
	double l = (goc_canvas_get_direction (canvas) == GOC_DIRECTION_RTL)? item->x1 - 1: item->x0;

	goc_canvas_c2w (canvas, l, item->y0, left, top);

	if (align == VALIGN_CENTER || align == VALIGN_DISTRIBUTED ||
	    align == VALIGN_BOTTOM) {
		int text_height, height = (int)(ie->item.y1 - ie->item.y0) * canvas->pixels_per_unit;
		pango_layout_get_pixel_size (ie->layout, NULL, &text_height);
		*top += (align != VALIGN_BOTTOM)
			? (height - text_height)/2
			: (height - text_height);
	}
}

static void
item_edit_draw (GocItem const *item, cairo_t *cr)
{
	ItemEdit  const *ie	= ITEM_EDIT (item);
	int top, left;
	GOColor color;
	int x0, y0, x1, y1; /* in widget coordinates */

	get_top_left (ie, &top, &left);
	if (goc_canvas_get_direction (item->canvas) == GOC_DIRECTION_RTL) {
		goc_canvas_c2w (item->canvas, item->x1, item->y0, &x0, &y0);
		goc_canvas_c2w (item->canvas, item->x0, item->y1, &x1, &y1);
	} else {
		goc_canvas_c2w (item->canvas, item->x0, item->y0, &x0, &y0);
		goc_canvas_c2w (item->canvas, item->x1, item->y1, &x1, &y1);
	}

	cairo_rectangle (cr, x0, y0, x1 - x0, y1 - y0);
	cairo_set_source_rgba (cr, 1., 1., 0.878431373, 1.);
	cairo_fill (cr);

	color = GO_COLOR_FROM_GDK (gtk_widget_get_style (GTK_WIDGET (item->canvas))->black);
	cairo_set_source_rgba (cr, GO_COLOR_TO_CAIRO (color));
	cairo_move_to (cr, left, top);
	pango_cairo_show_layout (cr, ie->layout);
	if (ie->cursor_visible) {
		PangoRectangle pos;
		char const *text = gtk_entry_get_text (ie->entry);
		int cursor_pos = gtk_editable_get_position (GTK_EDITABLE (ie->entry));
		pango_layout_index_to_pos (ie->layout,
			g_utf8_offset_to_pointer (text, cursor_pos) - text, &pos);
		cairo_set_line_width (cr, 1.);
		cairo_set_dash (cr, NULL, 0, 0.);
		cairo_set_line_cap (cr, CAIRO_LINE_CAP_BUTT);
		cairo_set_line_join (cr, CAIRO_LINE_JOIN_MITER);
		cairo_set_source_rgba (cr, 0., 0., 0., 1.);
		cairo_move_to (cr, left + PANGO_PIXELS (pos.x) + .5, top + PANGO_PIXELS (pos.y));
		cairo_line_to (cr, left + PANGO_PIXELS (pos.x) + .5, top + PANGO_PIXELS (pos.y + pos.height) - 1);
		cairo_stroke (cr);
	}
}

static double
item_edit_distance (GocItem *item, double cx, double cy,
		 GocItem **actual_item)
{
	*actual_item = NULL;
	if ((cx < item->x0) || (cy < item->y0) || (cx >= item->x1) || (cy >= item->y1))
		return 10000.0;

	*actual_item = item;
	return 0.0;
}

static gboolean
item_edit_enter_notify (GocItem *item, double x, double y)
{
	gnm_widget_set_cursor_type (GTK_WIDGET (item->canvas), GDK_XTERM);
	return TRUE;
}

static int
item_edit_button_pressed (GocItem *item, int button, double x, double y)
{
	if (button == 1) {
		ItemEdit *ie = ITEM_EDIT (item);
		GtkEditable *ed = GTK_EDITABLE (ie->entry);
		int target_index, trailing;
		int top, left;

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
	return FALSE;
}

static void
item_edit_update_bounds (GocItem *item)
{
	ItemEdit *ie = ITEM_EDIT (item);
	double scale = item->canvas->pixels_per_unit;

	if (ie->gfont != NULL) {
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
		pango_layout_set_width (ie->layout, (int)(item->x1 - item->x0)*PANGO_SCALE);

		attrs = wbcg_edit_get_markup (scg_wbcg (ie->scg), TRUE);
		if (attrs != NULL)
			attrs = pango_attr_list_copy (attrs);
		else
			attrs = gnm_style_generate_attrs_full (ie->style);

		/* reverse video the auto completion text  */
		if (entered_text != NULL && entered_text != text) {
			int const start = strlen (entered_text);
			GnmColor const *color = gnm_style_get_font_color (ie->style);
			attr = go_color_to_pango (color->go_color, FALSE);
			attr->start_index = start;
			attr->end_index = G_MAXINT;
			pango_attr_list_insert (attrs, attr);

			color = gnm_style_get_back_color (ie->style);
			attr = go_color_to_pango (color->go_color, TRUE);
			attr->start_index = start;
			attr->end_index = G_MAXINT;
			pango_attr_list_insert (attrs, attr);
		}
		pango_attr_list_insert_before (attrs,
					       pango_attr_scale_new (scale));

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
		if (merged != NULL)
			col = merged->end.col;

		while (col_size < width &&
		       col <= pane->last_full.col &&
		       col < gnm_sheet_get_last_col (sheet)) {
			ci = sheet_col_get_info (sheet, ++col);

			g_return_if_fail (ci != NULL);

			if (ci->visible)
				col_size += ci->size_pixels;
		}
		tmp = pane->first_offset.x + canvas->allocation.width;
		item->x1 = item->x0 + (col_size + GNM_COL_MARGIN + GNM_COL_MARGIN + 1) / scale;

		if (item->x1 >= tmp) {
			item->x1 = tmp;
			pango_layout_set_width (ie->layout, (item->x1 - item->x0 + 1)*PANGO_SCALE);
			pango_layout_get_pixel_size (ie->layout, &width, &height);
		}

		tmp = scg_colrow_distance_get (ie->scg, FALSE, ie->pos.row,
			(merged ? merged->end.row : ie->pos.row) + 1) - 1;
		item->y1 = item->y0 + (MAX (height, tmp)) / scale;
	}
}

static void
item_edit_realize (GocItem *item)
{
	ItemEdit *ie = ITEM_EDIT (item);
	if (parent_class->realize)
		(parent_class->realize) (item);

	ie->layout = gtk_widget_create_pango_layout (GTK_WIDGET (item->canvas), NULL);
	if (ie->scg)
		pango_layout_set_alignment (ie->layout,
			scg_sheet (ie->scg)->text_is_rtl ? PANGO_ALIGN_RIGHT : PANGO_ALIGN_LEFT);
}

static void
item_edit_unrealize (GocItem *item)
{
	ItemEdit *ie = ITEM_EDIT (item);

	g_object_unref (G_OBJECT (ie->layout));
	ie->layout = NULL;

	if (parent_class->unrealize)
		(parent_class->unrealize) (item);
}

static int
cb_item_edit_cursor_blink (ItemEdit *ie)
{
	GocItem *item = GOC_ITEM (ie);

	ie->cursor_visible = !ie->cursor_visible;

	goc_item_invalidate (item);
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
	ie->scg = NULL;
	ie->pos.col = -1;
	ie->pos.row = -1;
	ie->gfont = NULL;
	ie->style      = NULL;
	ie->cursor_visible = TRUE;
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
cb_entry_key_press (GocItem *item)
{
	goc_item_bounds_changed (item);
	return TRUE;
}

static int
cb_entry_cursor_event (GocItem *item)
{
	/* ensure we draw a cursor when moving quickly no matter what the
	 * current state is */
	ITEM_EDIT (item)->cursor_visible = TRUE;
	goc_item_invalidate (item);

	return TRUE;
}

static void
item_edit_set_property (GObject *gobject, guint param_id,
			GValue const *value, GParamSpec *pspec)
{
	GocItem *item      = GOC_ITEM (gobject);
	ItemEdit        *ie = ITEM_EDIT (gobject);
	GnmPane		*pane = GNM_PANE (item->canvas);
	SheetView const	*sv;
	double scale = item->canvas->pixels_per_unit;

	/* We can only set the sheet-control-gui once */
	g_return_if_fail (param_id == ARG_SHEET_CONTROL_GUI);
	g_return_if_fail (ie->scg == NULL);

	ie->scg = SHEET_CONTROL_GUI (g_value_get_object (value));

	sv = scg_view (ie->scg);
	ie->pos = sv->edit_pos;
	ie->entry = wbcg_get_entry (scg_wbcg (ie->scg));
	g_signal_connect_object (G_OBJECT (scg_wbcg (ie->scg)),
		"markup-changed",
		G_CALLBACK (goc_item_invalidate), G_OBJECT (ie), G_CONNECT_SWAPPED);
	g_signal_connect_object (G_OBJECT (gtk_widget_get_parent (GTK_WIDGET (ie->entry))),
		"changed",
		G_CALLBACK (goc_item_bounds_changed), G_OBJECT (ie), G_CONNECT_SWAPPED);
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
						sheet->context);
		gnm_font_ref (ie->gfont);

		if (gnm_style_get_align_h (ie->style) == HALIGN_GENERAL)
			gnm_style_set_align_h (ie->style, HALIGN_LEFT);

		/* move inwards 1 pixel from the grid line */
		item->y0 = (1 + pane->first_offset.y +
			scg_colrow_distance_get (ie->scg, FALSE,
				pane->first.row, ie->pos.row)) / scale;
		item->x0 = (1 + pane->first_offset.x +
			scg_colrow_distance_get (ie->scg, TRUE,
				pane->first.col, ie->pos.col)) / scale;

		item->x1 = item->x0 + 1 / scale;
		item->y1 = item->y0 + 1 / scale;
	}

	if (ie->layout)
		pango_layout_set_alignment (ie->layout,
			scg_sheet (ie->scg)->text_is_rtl ? PANGO_ALIGN_RIGHT : PANGO_ALIGN_LEFT);

	item_edit_cursor_blink_start (ie);
}

static void
item_edit_class_init (GObjectClass *gobject_class)
{
	GocItemClass *item_class = (GocItemClass *) gobject_class;

	parent_class = g_type_class_peek_parent (gobject_class);

	gobject_class->set_property = item_edit_set_property;
	gobject_class->dispose	   = item_edit_dispose;

	g_object_class_install_property (gobject_class, ARG_SHEET_CONTROL_GUI,
		g_param_spec_object ("SheetControlGUI", "SheetControlGUI",
			"the sheet control gui controlling the item",
			SHEET_CONTROL_GUI_TYPE,
			/* resist the urge to use G_PARAM_CONSTRUCT_ONLY
			 * We are going through goc_item_new, which
			 * calls g_object_new assigns the parent pointer before
			 * setting the construction parameters */
			 GSF_PARAM_STATIC | G_PARAM_WRITABLE));

	/* GocItem method overrides */
	item_class->realize        = item_edit_realize;
	item_class->unrealize      = item_edit_unrealize;
	item_class->draw           = item_edit_draw;
	item_class->distance       = item_edit_distance;
	item_class->update_bounds  = item_edit_update_bounds;
	item_class->button_pressed = item_edit_button_pressed;
	item_class->enter_notify   = item_edit_enter_notify;
}

GSF_CLASS (ItemEdit, item_edit,
	   item_edit_class_init, item_edit_init,
	   GOC_TYPE_ITEM)

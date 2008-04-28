/* vim: set sw=8: */
/*
 * A canvas item implementing row/col headers with support for outlining.
 *
 * Author:
 *     Miguel de Icaza (miguel@kernel.org)
 *     Jody Goldberg   (jody@gnome.org)
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include "gnumeric.h"
#include "item-bar.h"
#include "gnm-pane-impl.h"

#include "style-color.h"
#include "sheet.h"
#include "sheet-control-gui.h"
#include "sheet-control-gui-priv.h"
#include "application.h"
#include "selection.h"
#include "wbc-gtk.h"
#include "gui-util.h"
#include "parse-util.h"
#include "commands.h"

#include <gsf/gsf-impl-utils.h>
#include <gtk/gtklabel.h>
#define GNUMERIC_ITEM "BAR"
#include "item-debug.h"

#include <string.h>

struct _ItemBar {
	FooCanvasItem	 base;

	GnmPane		*pane;
	GdkGC           *text_gc, *filter_gc, *lines, *shade;
	GdkCursor       *normal_cursor;
	GdkCursor       *change_cursor;
	PangoFont	*normal_font, *bold_font;
	int             normal_font_ascent, bold_font_ascent;
	GtkWidget       *tip;			/* Tip for scrolling */
	gboolean	 dragging;
	gboolean	 is_col_header;
	gboolean	 has_resize_guides;
	int		 indent, cell_width, cell_height;
	int		 start_selection;	/* Where selection started */
	int		 colrow_being_resized;
	int		 colrow_resize_size;
	int		 resize_start_pos;

	struct {
		PangoItem	 *item;
		PangoGlyphString *glyphs;
	} pango;
};

typedef FooCanvasItemClass ItemBarClass;
static FooCanvasItemClass *parent_class;

enum {
	ITEM_BAR_PROP_0,
	ITEM_BAR_PROP_PANE,
	ITEM_BAR_PROP_IS_COL_HEADER
};

static int
ib_compute_pixels_from_indent (Sheet const *sheet, gboolean const is_cols)
{
	double const scale =
		sheet->last_zoom_factor_used *
		gnm_app_display_dpi_get (is_cols) / 72.;
	int const indent = is_cols
		? sheet->cols.max_outline_level
		: sheet->rows.max_outline_level;
	if (!sheet->display_outlines || indent <= 0)
		return 0;
	return (int)(5 + (indent + 1) * 14 * scale + 0.5);
}

static void
ib_fonts_unref (ItemBar *ib)
{
	if (ib->normal_font != NULL) {
		g_object_unref (ib->normal_font);
		ib->normal_font = NULL;
	}

	if (ib->bold_font != NULL) {
		g_object_unref (ib->bold_font);
		ib->bold_font = NULL;
	}
}

/**
 * item_bar_calc_size :
 * @ib : #ItemBar
 *
 * Scale fonts and sizes by the pixels_per_unit of the associated sheet.
 *
 * Returns : the size of the fixed dimension.
 **/
int
item_bar_calc_size (ItemBar *ib)
{
	SheetControlGUI	* const scg = ib->pane->simple.scg;
	Sheet const *sheet = scg_sheet (scg);
	double const zoom_factor = sheet->last_zoom_factor_used;
	PangoContext *context;
	PangoFontDescription const *src_desc = wbcg_get_font_desc (scg_wbcg (scg));
	PangoFontDescription *desc;
	int size = pango_font_description_get_size (src_desc);
	PangoLayout *layout;
	PangoRectangle ink_rect, logical_rect;
	gboolean const char_label = ib->is_col_header && !sheet->convs->r1c1_addresses;

	ib_fonts_unref (ib);

	context = gtk_widget_get_pango_context (GTK_WIDGET (ib->pane));
	desc = pango_font_description_copy (src_desc);
	pango_font_description_set_size (desc, zoom_factor * size);
	layout = pango_layout_new (context);

	/*
	 * Figure out how tall the label can be.
	 * (Note that we avoid J/Q/Y which may go below the line.)
	 */
	pango_layout_set_text (layout,
			       char_label ? "AHW" : "0123456789",
			       -1);
	ib->normal_font = pango_context_load_font (context, desc);
	pango_layout_set_font_description (layout, desc);
	pango_layout_get_extents (layout, &ink_rect, NULL);
	ib->normal_font_ascent = PANGO_PIXELS (ink_rect.height + ink_rect.y);

	/*
	 * Use the size of the bold header font to size the free dimensions.
	 * Add 2 pixels above and below.
	 */
	pango_font_description_set_weight (desc, PANGO_WEIGHT_BOLD);
	ib->bold_font = pango_context_load_font (context, desc);
	pango_layout_set_font_description (layout, desc);
	pango_layout_get_extents (layout, &ink_rect, &logical_rect);
	ib->cell_height = 2 + 2 + PANGO_PIXELS (logical_rect.height);
	ib->bold_font_ascent = PANGO_PIXELS (ink_rect.height + ink_rect.y);

	/* 5 pixels left and right plus the width of the widest string I can think of */
	if (char_label)
		pango_layout_set_text (layout, "WWWWWWWWWW", strlen (col_name (gnm_sheet_get_max_cols (sheet) - 1)));
	else
		pango_layout_set_text (layout, "8888888888", strlen (row_name (gnm_sheet_get_max_rows (sheet) - 1)));
	pango_layout_get_extents (layout, NULL, &logical_rect);
	ib->cell_width = 5 + 5 + PANGO_PIXELS (logical_rect.width);

	pango_font_description_free (desc);
	g_object_unref (layout);

	ib->pango.item->analysis.font = g_object_ref (ib->normal_font);
	ib->pango.item->analysis.language =
		pango_context_get_language (context);
	ib->pango.item->analysis.shape_engine =
		pango_font_find_shaper (ib->normal_font,
					ib->pango.item->analysis.language,
					'A');

	ib->indent = ib_compute_pixels_from_indent (sheet, ib->is_col_header);
	foo_canvas_item_request_update (&ib->base);

	return ib->indent +
		(ib->is_col_header ? ib->cell_height : ib->cell_width);
}

PangoFont *
item_bar_normal_font (ItemBar const *ib)
{
	return ib->normal_font;
}

int
item_bar_indent	(ItemBar const *ib)
{
	return ib->indent;
}

static void
item_bar_update (FooCanvasItem *item,  double i2w_dx, double i2w_dy, int flags)
{
	ItemBar *ib = ITEM_BAR (item);

	item->x1 = 0;
	item->y1 = 0;
	if (ib->is_col_header) {
		item->x2 = G_MAXINT/2;
		item->y2 = (ib->cell_height + ib->indent);
	} else {
		item->x2 = (ib->cell_width  + ib->indent);
		item->y2 = G_MAXINT/2;
	}

	if (parent_class->update)
		(*parent_class->update)(item, i2w_dx, i2w_dy, flags);
}

static void
item_bar_realize (FooCanvasItem *item)
{
	ItemBar *ib;
	GdkWindow *window;
	GtkStyle *style;
	GdkDisplay *display;

	if (parent_class->realize)
		(*parent_class->realize)(item);

	ib = ITEM_BAR (item);
	window = GTK_WIDGET (item->canvas)->window;

	/* Configure our gc */
	style = gtk_widget_get_style (GTK_WIDGET (item->canvas));

	ib->text_gc = gdk_gc_new (window);
	gdk_gc_set_rgb_fg_color (ib->text_gc, &style->text[GTK_STATE_NORMAL]);
	ib->filter_gc = gdk_gc_new (window);
	gdk_gc_set_rgb_fg_color (ib->filter_gc, &gs_yellow);
	ib->shade = gdk_gc_new (window);
	gdk_gc_set_rgb_fg_color (ib->shade, &style->dark[GTK_STATE_NORMAL]);
	ib->lines = gdk_gc_new (window);
	gdk_gc_copy (ib->lines, ib->text_gc);
	gdk_gc_set_line_attributes (ib->lines, 2, GDK_LINE_SOLID,
				    GDK_CAP_NOT_LAST, GDK_JOIN_MITER);

	display = gtk_widget_get_display (GTK_WIDGET (item->canvas));
	ib->normal_cursor = gdk_cursor_new_for_display (display, GDK_LEFT_PTR);
	if (ib->is_col_header)
		ib->change_cursor = gdk_cursor_new_for_display (display, GDK_SB_H_DOUBLE_ARROW);
	else
		ib->change_cursor = gdk_cursor_new_for_display (display, GDK_SB_V_DOUBLE_ARROW);
	item_bar_calc_size (ib);
}

static void
item_bar_unrealize (FooCanvasItem *item)
{
	ItemBar *ib = ITEM_BAR (item);

	g_object_unref (G_OBJECT (ib->text_gc));
	g_object_unref (G_OBJECT (ib->filter_gc));
	g_object_unref (G_OBJECT (ib->lines));
	g_object_unref (G_OBJECT (ib->shade));
	gdk_cursor_unref (ib->change_cursor);
	gdk_cursor_unref (ib->normal_cursor);

	if (parent_class->unrealize)
		(*parent_class->unrealize)(item);
}

static void
ib_draw_cell (ItemBar const * const ib, GdkDrawable *drawable,
	      GdkGC *text_gc, ColRowSelectionType const type,
	      char const * const str, GdkRectangle *rect)
{
	GtkWidget	*canvas = GTK_WIDGET (ib->base.canvas);
	GdkGC		*gc;
	PangoFont	*font;
	PangoRectangle   size;
	int shadow, ascent;

	switch (type) {
	default:
	case COL_ROW_NO_SELECTION:
		shadow = GTK_SHADOW_OUT;
		gc     = canvas->style->bg_gc [GTK_STATE_ACTIVE];
		font   = ib->normal_font;
		ascent = ib->normal_font_ascent;
		break;

	case COL_ROW_PARTIAL_SELECTION:
		shadow = GTK_SHADOW_OUT;
		gc     = canvas->style->dark_gc [GTK_STATE_PRELIGHT];
		font   = ib->bold_font;
		ascent = ib->bold_font_ascent;
		break;

	case COL_ROW_FULL_SELECTION:
		shadow = GTK_SHADOW_IN;
		gc     = canvas->style->dark_gc [GTK_STATE_NORMAL];
		font   = ib->bold_font;
		ascent = ib->bold_font_ascent;
		break;
	}
	/* When we are really small leave out the shadow and the text */
	if (rect->width <= 2 || rect->height <= 2) {
		gdk_draw_rectangle (drawable, gc, TRUE,
			rect->x, rect->y, rect->width, rect->height);
		return;
	}

	gdk_draw_rectangle (drawable, gc, TRUE,
		rect->x + 1, rect->y + 1, rect->width - 1, rect->height - 1);
	gtk_paint_shadow (canvas->style, drawable, GTK_STATE_NORMAL, shadow,
			  NULL, NULL, "GnmItemBarCell",
			  rect->x, rect->y, rect->width + 1, rect->height + 1);

	g_return_if_fail (font != NULL);
	g_object_unref (ib->pango.item->analysis.font);
	ib->pango.item->analysis.font = g_object_ref (font);
	pango_shape (str, strlen (str), &(ib->pango.item->analysis), ib->pango.glyphs);
	pango_glyph_string_extents (ib->pango.glyphs, font, NULL, &size);

	gdk_gc_set_clip_rectangle (text_gc, rect);
	gdk_draw_glyphs (drawable, text_gc, font,
		rect->x + (rect->width - PANGO_PIXELS (size.width)) / 2,
		rect->y + (rect->height - PANGO_PIXELS (size.height)) / 2 + ascent,
		ib->pango.glyphs);
}

int
item_bar_group_size (ItemBar const *ib, int max_outline)
{
	return (max_outline > 0) ? (ib->indent - 2) / (max_outline + 1) : 0;
}

static void
item_bar_draw (FooCanvasItem *item, GdkDrawable *drawable, GdkEventExpose *expose)
{
	ItemBar const         *ib = ITEM_BAR (item);
	GnmPane	 const	      *pane = ib->pane;
	SheetControlGUI const *scg    = pane->simple.scg;
	Sheet const           *sheet  = scg_sheet (scg);
	SheetView const	      *sv     = scg_view (scg);
	GtkWidget *canvas = GTK_WIDGET (item->canvas);
	ColRowInfo const *cri, *next = NULL;
	int pixels;
	gboolean prev_visible;
	gboolean const draw_below = sheet->outline_symbols_below != FALSE;
	gboolean const draw_right = sheet->outline_symbols_right != FALSE;
	int prev_level;
	GdkRectangle rect;
	GdkPoint points[3];
	gboolean const has_object = scg->new_object != NULL || scg->selected_objects != NULL;
	gboolean const rtl = sheet->text_is_rtl != FALSE;
	int shadow;
	int first_line_offset = 1;

	if (ib->is_col_header) {
		int const inc = item_bar_group_size (ib, sheet->cols.max_outline_level);
		int const base_pos = .2 * inc;
		int const len = (inc > 4) ? 4 : inc;
		int end = expose->area.x;
		int total, col = pane->first.col;
		gboolean const char_label = !sheet->convs->r1c1_addresses;

		/* shadow type selection must be keep in sync with code in ib_draw_cell */
		rect.y = ib->indent;
		rect.height = ib->cell_height;
		shadow = (col > 0 && !has_object && sv_selection_col_type (sv, col-1) == COL_ROW_FULL_SELECTION)
			? GTK_SHADOW_IN : GTK_SHADOW_OUT;

		if (rtl)
			total = gnm_foo_canvas_x_w2c (item->canvas, pane->first_offset.col);
		else {
			total = pane->first_offset.col;
			end += expose->area.width;
		}

		if (col > 0) {
			cri = sheet_col_get_info (sheet, col-1);
			prev_visible = cri->visible;
			prev_level = cri->outline_level;
		} else {
			prev_visible = TRUE;
			prev_level = 0;
		}

		do {
			if (col >= gnm_sheet_get_max_cols (sheet))
				return;

			/* DO NOT enable resizing all until we get rid of
			 * resize_start_pos.  It will be wrong if things ahead
			 * of it move
			 */
			cri = sheet_col_get_info (sheet, col);
			if (col != -1 && ib->colrow_being_resized == col)
			/* || sv_is_colrow_selected (sheet, col, TRUE))) */
				pixels = ib->colrow_resize_size;
			else
				pixels = cri->size_pixels;

			if (cri->visible) {
				int left, level, i = 0, pos = base_pos;

				if (rtl) {
					left = (total -= pixels);
					rect.x = total;
				} else {
					rect.x = left = total;
					total += pixels;
				}

				rect.width = pixels;
				ib_draw_cell (ib, drawable, ib->text_gc,
					       has_object
						       ? COL_ROW_NO_SELECTION
						       : sv_selection_col_type (sv, col),
					       char_label
						       ? col_name (col)
						       : row_name (col),
					       &rect);

				if (len > 0) {
					if (!draw_right) {
						next = sheet_col_get_info (sheet, col + 1);
						prev_level = next->outline_level;
						prev_visible = next->visible;
						points[0].x = rtl ? (total + pixels) : left;
					} else
						points[0].x = total;

					/* draw the start or end marks and the vertical lines */
					points[1].x = points[2].x = left + pixels/2;
					for (level = cri->outline_level; i++ < level ; pos += inc) {
						points[0].y = points[1].y = pos;
						points[2].y		  = pos + len;
						if (i > prev_level)
							gdk_draw_lines (drawable, ib->lines, points, 3);
						else if (rtl)
							gdk_draw_line (drawable, ib->lines,
								       left - first_line_offset, pos,
								       total + pixels,		 pos);
						else
							gdk_draw_line (drawable, ib->lines,
								       left - first_line_offset, pos,
								       total,			 pos);
					}
					first_line_offset = 0;

					if (draw_right ^ rtl) {
						if (prev_level > level) {
							int safety = 0;
							int top = pos - base_pos + 2; /* inside cell's shadow */
							int size = inc < pixels ? inc : pixels;

							if (size > 15)
								size = 15;
							else if (size < 6)
								safety = 6 - size;

							gtk_paint_shadow (canvas->style, drawable,
								 GTK_STATE_NORMAL,
								 prev_visible ? GTK_SHADOW_OUT : GTK_SHADOW_IN,
								 NULL, NULL, "GnmItemBarCell",
								 left, top+safety, size, size);
							if (size > 9) {
								if (!prev_visible) {
									top++;
									left++;
									gdk_draw_line (drawable, ib->lines,
										       left+size/2, top+3,
										       left+size/2, top+size-4);
								}
								gdk_draw_line (drawable, ib->lines,
									       left+3,	    top+size/2,
									       left+size-4, top+size/2);
							}
						} else if (level > 0)
							gdk_draw_line (drawable, ib->lines,
								       left+pixels/2, pos,
								       left+pixels/2, pos+len);
					} else {
						if (prev_level > level) {
							int safety = 0;
							int top = pos - base_pos + 2; /* inside cell's shadow */
							int size = inc < pixels ? inc : pixels;
							int right;

							if (size > 15)
								size = 15;
							else if (size < 6)
								safety = 6 - size;

							right = (rtl ? (total + pixels) : total) - size;
							gtk_paint_shadow (canvas->style, drawable,
								 GTK_STATE_NORMAL,
								 prev_visible ? GTK_SHADOW_OUT : GTK_SHADOW_IN,
								 NULL, NULL, "GnmItemBarCell",
								 right, top+safety, size, size);
							if (size > 9) {
								if (!prev_visible) {
									top++;
									right++;
									gdk_draw_line (drawable, ib->lines,
										       right+size/2, top+3,
										       right+size/2, top+size-4);
								}
								gdk_draw_line (drawable, ib->lines,
									       right+3,	    top+size/2,
									       right+size-4, top+size/2);
							}
						} else if (level > 0)
							gdk_draw_line (drawable, ib->lines,
								       left+pixels/2, pos,
								       left+pixels/2, pos+len);
					}
				}
			}
			prev_visible = cri->visible;
			prev_level = cri->outline_level;
			++col;
		} while ((rtl && end <= total) || (!rtl && total <= end));
	} else {
		int const inc = item_bar_group_size (ib, sheet->rows.max_outline_level);
		int base_pos = .2 * inc;
		int const len = (inc > 4) ? 4 : inc;
		int const end = expose->area.y + expose->area.height;
		int const dir = rtl ? -1 : 1;

		int total = pane->first_offset.row;
		int row = pane->first.row;

		if (rtl) {
			base_pos = ib->indent + ib->cell_width - base_pos;
			/* Move header bar 1 pixel to the left. */
			rect.x = -1;
		} else
			rect.x = ib->indent;
		rect.width = ib->cell_width;

		if (row > 0) {
			cri = sheet_row_get_info (sheet, row-1);
			prev_visible = cri->visible;
			prev_level = cri->outline_level;
		} else {
			prev_visible = TRUE;
			prev_level = 0;
		}

		do {
			if (row >= gnm_sheet_get_max_rows (sheet))
				return;

			/* DO NOT enable resizing all until we get rid of
			 * resize_start_pos.  It will be wrong if things ahead
			 * of it move
			 */
			cri = sheet_row_get_info (sheet, row);
			if (row != -1 && ib->colrow_being_resized == row)
			/* || sv_is_colrow_selected (sheet, row, FALSE))) */
				pixels = ib->colrow_resize_size;
			else
				pixels = cri->size_pixels;

			if (cri->visible) {
				int level, i = 0, pos = base_pos;
				int top = total;

				total += pixels;
				rect.y = top;
				rect.height = pixels;
				ib_draw_cell (ib, drawable,
					      cri->in_filter
						      ? ib->filter_gc : ib->text_gc,
					      has_object
						      ? COL_ROW_NO_SELECTION
						      : sv_selection_row_type (sv, row),
					      row_name (row), &rect);

				if (len > 0) {
					if (!draw_below) {
						next = sheet_row_get_info (sheet, row + 1);
						points[0].y = top;
					} else
						points[0].y = total;

					/* draw the start or end marks and the vertical lines */
					points[1].y = points[2].y = top + pixels/2;
					for (level = cri->outline_level; i++ < level ; pos += inc * dir) {
						points[0].x = points[1].x = pos;
						points[2].x		  = pos + len * dir;
						if (draw_below && i > prev_level)
							gdk_draw_lines (drawable, ib->lines, points, 3);
						else if (!draw_below && i > next->outline_level)
							gdk_draw_lines (drawable, ib->lines, points, 3);
						else
							gdk_draw_line (drawable, ib->lines,
								       pos, top - first_line_offset,
								       pos, total);
					}
					first_line_offset = 0;

					if (draw_below) {
						if (prev_level > level) {
							int left, safety = 0;
							int size = inc < pixels ? inc : pixels;

							if (size > 15)
								size = 15;
							else if (size < 6)
								safety = 6 - size;

							/* inside cell's shadow */
							left = pos - dir * (.2 * inc - 2);
							if (rtl)
								left -= size;
							gtk_paint_shadow (canvas->style, drawable,
								 GTK_STATE_NORMAL,
								 prev_visible ? GTK_SHADOW_OUT : GTK_SHADOW_IN,
								 NULL, NULL, "GnmItemBarCell",
								 left+safety, top, size, size);
							if (size > 9) {
								if (!prev_visible) {
									left += dir;
									top++;
									gdk_draw_line (drawable, ib->lines,
										       left+size/2, top+3,
										       left+size/2, top+size-4);
								}
								gdk_draw_line (drawable, ib->lines,
									       left+3,	    top+size/2,
									       left+size-4, top+size/2);
							}
						} else if (level > 0)
							gdk_draw_line (drawable, ib->lines,
								       pos,      top+pixels/2,
								       pos+len,  top+pixels/2);
					} else {
						if (next->outline_level > level) {
							int left, safety = 0;
							int size = inc < pixels ? inc : pixels;
							int bottom;

							if (size > 15)
								size = 15;
							else if (size < 6)
								safety = 6 - size;

							/* inside cell's shadow */
							left = pos - dir * (.2 * inc - 2);
							if (rtl)
								left -= size;
							bottom = total - size;
							gtk_paint_shadow (canvas->style, drawable,
								 GTK_STATE_NORMAL,
								 next->visible ? GTK_SHADOW_OUT : GTK_SHADOW_IN,
								 NULL, NULL, "GnmItemBarCell",
								 left+safety*dir, bottom, size, size);
							if (size > 9) {
								if (!next->visible) {
									left += dir;
									top++;
									gdk_draw_line (drawable, ib->lines,
										       left+size/2, bottom+3,
										       left+size/2, bottom+size-4);
								}
								gdk_draw_line (drawable, ib->lines,
									       left+3,	    bottom+size/2,
									       left+size-4, bottom+size/2);
							}
						} else if (level > 0)
							gdk_draw_line (drawable, ib->lines,
								       pos,      top+pixels/2,
								       pos+len,  top+pixels/2);
					}
				}
			}
			prev_visible = cri->visible;
			prev_level = cri->outline_level;
			++row;
		} while (total <= end);
	}
}

static double
item_bar_point (FooCanvasItem *item, double x, double y, int cx, int cy,
		FooCanvasItem **actual_item)
{
	*actual_item = item;
	return 0.0;
}

/**
 * is_pointer_on_division :
 * @ib : #ItemBar
 * @x  : in world coords
 * @y  : in world coords
 * @the_total :
 * @the_element :
 * @minor_pos :
 *
 * NOTE : this could easily be optimized.  We need not start at 0 every time.
 *        We could potentially use the routines in gnm-pane.
 *
 * Returns non-NULL if point (@x,@y) is on a division
 **/
static ColRowInfo const *
is_pointer_on_division (ItemBar const *ib, double x, double y,
			int *the_total, int *the_element, int *minor_pos)
{
	Sheet *sheet = scg_sheet (ib->pane->simple.scg);
	ColRowInfo const *cri;
	double const scale = ib->base.canvas->pixels_per_unit;
	int major, minor, i, total = 0;

	x *= scale;
	y *= scale;
	if (ib->is_col_header) {
		major = x;
		minor = y;
	} else {
		major = y;
		minor = sheet->text_is_rtl ? (ib->cell_width + ib->indent - x) : x;
	}
	if (NULL != minor_pos)
		*minor_pos = minor;
	if (ib->is_col_header && sheet->text_is_rtl)
		major = -major;
	if (NULL != the_element)
		*the_element = -1;
	for (i = 0; total < major; i++) {
		if (ib->is_col_header) {
			if (i >= gnm_sheet_get_max_cols (sheet))
				return NULL;
			cri = sheet_col_get_info (sheet, i);
		} else {
			if (i >= gnm_sheet_get_max_rows (sheet))
				return NULL;
			cri = sheet_row_get_info (sheet, i);
		}

		if (cri->visible) {
			WBCGtk *wbcg = scg_wbcg (ib->pane->simple.scg);
			total += cri->size_pixels;

			if (wbc_gtk_get_guru (wbcg) == NULL &&
			    !wbcg_is_editing (wbcg) &&
			    (total - 4 < major) && (major < total + 4)) {
				if (the_total)
					*the_total = total;
				if (the_element)
					*the_element = i;
				return (minor >= ib->indent) ? cri : NULL;
			}
		}

		if (total > major) {
			if (the_element)
				*the_element = i;
			return NULL;
		}
	}
	return NULL;
}

/* x & y in world coords */
static void
ib_set_cursor (ItemBar *ib, double x, double y)
{
	GdkWindow *window = GTK_WIDGET (ib->base.canvas)->window;
	GdkCursor *cursor = ib->normal_cursor;

	/* We might be invoked before we are realized */
	if (NULL == window)
		return;
	if (NULL != is_pointer_on_division (ib, x, y, NULL, NULL, NULL))
		cursor = ib->change_cursor;
	gdk_window_set_cursor (window, cursor);
}

static void
colrow_tip_setlabel (ItemBar *ib, gboolean const is_cols, int size_pixels)
{
	if (ib->tip != NULL) {
		char *buffer;
		double const scale = 72. / gnm_app_display_dpi_get (!is_cols);
		if (is_cols)
			buffer = g_strdup_printf (_("Width: %.2f pts (%d pixels)"),
						  scale*size_pixels, size_pixels);
		else
			buffer = g_strdup_printf (_("Height: %.2f pts (%d pixels)"),
						  scale*size_pixels, size_pixels);
		gtk_label_set_text (GTK_LABEL (ib->tip), buffer);
		g_free(buffer);
	}
}

static void
item_bar_resize_stop (ItemBar *ib, int new_size)
{
	if (new_size != 0 && ib->colrow_being_resized >= 0)
		scg_colrow_size_set (ib->pane->simple.scg,
				     ib->is_col_header,
				     ib->colrow_being_resized, new_size);
	ib->colrow_being_resized = -1;
	ib->has_resize_guides = FALSE;
	scg_size_guide_stop (ib->pane->simple.scg);

	if (ib->tip != NULL) {
		gtk_widget_destroy (gtk_widget_get_toplevel (ib->tip));
		ib->tip = NULL;
	}
}

static gboolean
cb_extend_selection (GnmPane *pane, GnmPaneSlideInfo const *info)
{
	ItemBar * const ib = info->user_data;
	gboolean const is_cols = ib->is_col_header;
	scg_colrow_select (pane->simple.scg,
		is_cols, is_cols ? info->col : info->row, GDK_SHIFT_MASK);
	return TRUE;
}

static gint
outline_button_press (ItemBar const *ib, int element, int pixel)
{
	SheetControlGUI *scg = ib->pane->simple.scg;
	Sheet * const sheet = scg_sheet (scg);
	int inc, step;

	if (ib->is_col_header) {
		if (sheet->cols.max_outline_level <= 0)
			return TRUE;
		inc = (ib->indent - 2) / (sheet->cols.max_outline_level + 1);
	} else {
		if (sheet->rows.max_outline_level <= 0)
			return TRUE;
		inc = (ib->indent - 2) / (sheet->rows.max_outline_level + 1);
	}

	step = pixel / inc;

	cmd_selection_outline_change (scg_wbc (scg), ib->is_col_header,
				      element, step);
	return TRUE;
}

static gint
item_bar_event (FooCanvasItem *item, GdkEvent *e)
{
	ColRowInfo const *cri;
	FooCanvas	* const canvas = item->canvas;
	ItemBar		* const ib = ITEM_BAR (item);
	GnmPane		* const pane = ib->pane;
	SheetControlGUI	* const scg = pane->simple.scg;
	SheetControl	* const sc = (SheetControl *) pane->simple.scg;
	Sheet		* const sheet = sc_sheet (sc);
	WBCGtk * const wbcg = scg_wbcg (scg);
	gboolean const is_cols = ib->is_col_header;
	int pos, minor_pos, start, element, x, y;

	/* NOTE :
	 * No need to map coordinates since we do the zooming of the item bars manually
	 * there is no transform needed.
	 */
	switch (e->type){
	case GDK_ENTER_NOTIFY:
		ib_set_cursor (ib, e->crossing.x, e->crossing.y);
		break;

	case GDK_MOTION_NOTIFY:
		foo_canvas_w2c (canvas, e->motion.x, e->motion.y, &x, &y);

		/* Do col/row resizing or incremental marking */
		if (ib->colrow_being_resized != -1) {
			int new_size;
			if (!ib->has_resize_guides) {
				ib->has_resize_guides = TRUE;
				scg_size_guide_start (ib->pane->simple.scg,
					ib->is_col_header, ib->colrow_being_resized, 1);

				gnm_simple_canvas_grab (item,
					GDK_POINTER_MOTION_MASK | GDK_BUTTON_RELEASE_MASK,
					ib->change_cursor, e->motion.time);
			}

			cri = sheet_colrow_get_info (sheet,
				ib->colrow_being_resized, is_cols);
			pos = is_cols ? (sheet->text_is_rtl ? -e->motion.x : e->motion.x) : e->motion.y;
			pos *= ib->base.canvas->pixels_per_unit;
			new_size = pos - ib->resize_start_pos;
			if (is_cols && sheet->text_is_rtl)
				new_size += cri->size_pixels;

			/* Ensure we always have enough room for the margins */
			if (is_cols) {
				if (new_size <= (GNM_COL_MARGIN + GNM_COL_MARGIN)) {
					new_size = GNM_COL_MARGIN + GNM_COL_MARGIN + 1;
					pos = pane->first_offset.col +
						scg_colrow_distance_get (scg, TRUE,
							pane->first.col,
							ib->colrow_being_resized);
					pos += new_size;
				}
			} else {
				if (new_size <= (GNM_ROW_MARGIN + GNM_ROW_MARGIN)) {
					new_size = GNM_ROW_MARGIN + GNM_ROW_MARGIN + 1;
					pos = pane->first_offset.row +
						scg_colrow_distance_get (scg, FALSE,
							pane->first.row,
							ib->colrow_being_resized);
					pos += new_size;
				}
			}

			ib->colrow_resize_size = new_size;
			colrow_tip_setlabel (ib, is_cols, new_size);
			scg_size_guide_motion (scg, is_cols, pos);

			/* Redraw the ItemBar to show nice incremental progress */
			foo_canvas_request_redraw (canvas, 0, 0, G_MAXINT/2,  G_MAXINT/2);

		} else if (ib->start_selection != -1) {

			gnm_pane_handle_motion (ib->pane,
				canvas, &e->motion,
				GNM_PANE_SLIDE_AT_COLROW_BOUND |
					(is_cols ? GNM_PANE_SLIDE_X : GNM_PANE_SLIDE_Y),
				cb_extend_selection, ib);
		} else
			ib_set_cursor (ib, e->motion.x, e->motion.y);
		break;

	case GDK_BUTTON_PRESS:
		/* Ignore scroll wheel events */
		if (e->button.button > 3)
			return FALSE;

		if (wbc_gtk_get_guru (wbcg) == NULL)
			scg_mode_edit (scg);

		cri = is_pointer_on_division (ib, e->button.x, e->button.y,
			&start, &element, &minor_pos);
		if (element < 0)
			return FALSE;
		if (minor_pos < ib->indent)
			return outline_button_press (ib, element, minor_pos);

		if (e->button.button == 3) {
			if (wbc_gtk_get_guru (wbcg) != NULL)
				return TRUE;
			/* If the selection does not contain the current row/col
			 * then clear the selection and add it.
			 */
			if (!sv_is_colrow_selected (sc_view (sc), element, is_cols))
				scg_colrow_select (scg, is_cols,
						   element, e->button.state);

			scg_context_menu (scg, &e->button, is_cols, !is_cols);
		} else if (cri != NULL) {
			/*
			 * Record the important bits.
			 *
			 * By setting colrow_being_resized to a non -1 value,
			 * we know that we are being resized (used in the
			 * other event handlers).
			 */
			ib->colrow_being_resized = element;
			ib->resize_start_pos = (is_cols && sheet->text_is_rtl)
				? start : (start - cri->size_pixels);
			ib->colrow_resize_size = cri->size_pixels;

			if (ib->tip == NULL) {
				ib->tip = gnumeric_create_tooltip ();
				colrow_tip_setlabel (ib, is_cols, ib->colrow_resize_size);
				/* Position above the current point for both
				 * col and row headers.  trying to put it
				 * beside for row headers often ends up pushing
				 * the tip under the cursor which can have odd
				 * effects on the event stream.  win32 was
				 * different from X. */
				gnumeric_position_tooltip (ib->tip, TRUE);
				gtk_widget_show_all (gtk_widget_get_toplevel (ib->tip));
			}
		} else {
			if (wbc_gtk_get_guru (wbcg) != NULL &&
			    !wbcg_entry_has_logical (wbcg))
				break;

			/* If we're editing it is possible for this to fail */
			if (!scg_colrow_select (scg, is_cols, element, e->button.state))
				break;

			ib->start_selection = element;
			gnm_pane_slide_init (pane);
			gnm_simple_canvas_grab (item,
				GDK_POINTER_MOTION_MASK |
				GDK_BUTTON_RELEASE_MASK,
				ib->normal_cursor,
				e->button.time);

		}
		break;

	case GDK_2BUTTON_PRESS:
		/* Ignore scroll wheel events */
		if (e->button.button > 3)
			return FALSE;

		if (e->button.button != 3)
			item_bar_resize_stop (ib, -1);
		break;

	case GDK_BUTTON_RELEASE: {
		gboolean needs_ungrab = FALSE;

		/* Ignore scroll wheel events */
		if (e->button.button > 3)
			return FALSE;

		gnm_pane_slide_stop (ib->pane);

		if (ib->start_selection >= 0) {
			needs_ungrab = TRUE;
			ib->start_selection = -1;
			gnm_expr_entry_signal_update (
				wbcg_get_entry_logical (wbcg), TRUE);
		}
		if (ib->colrow_being_resized >= 0) {
			if (ib->has_resize_guides) {
				needs_ungrab = TRUE;
				item_bar_resize_stop (ib, ib->colrow_resize_size);
			} else
				/*
				 * No need to resize, nothing changed.
				 * This will handle the case of a double click.
				 */
				item_bar_resize_stop (ib, 0);
		}
		if (needs_ungrab)
			gnm_simple_canvas_ungrab (item, e->button.time);
		break;
	}

	default:
		return FALSE;
	}
	return TRUE;
}

static void
item_bar_set_property (GObject *obj, guint param_id,
		       GValue const *value, GParamSpec *pspec)
{
	ItemBar *ib = ITEM_BAR (obj);

	switch (param_id){
	case ITEM_BAR_PROP_PANE:
		ib->pane = g_value_get_object (value);
		break;
	case ITEM_BAR_PROP_IS_COL_HEADER:
		ib->is_col_header = g_value_get_boolean (value);
		break;
	}
	foo_canvas_item_request_update (&ib->base);
}

static void
item_bar_dispose (GObject *obj)
{
	ItemBar *ib = ITEM_BAR (obj);

	ib_fonts_unref (ib);

	if (ib->tip) {
		gtk_object_destroy (GTK_OBJECT (ib->tip));
		ib->tip = NULL;
	}

	if (ib->pango.glyphs != NULL) {
		pango_glyph_string_free (ib->pango.glyphs);
		ib->pango.glyphs = NULL;
	}
	if (ib->pango.item != NULL) {
		pango_item_free (ib->pango.item);
		ib->pango.item = NULL;
	}

	G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
item_bar_init (ItemBar *ib)
{
	ib->base.x1 = 0;
	ib->base.y1 = 0;
	ib->base.x2 = 0;
	ib->base.y2 = 0;

	ib->dragging = FALSE;
	ib->is_col_header = FALSE;
	ib->cell_width = ib->cell_height = 1;
	ib->indent = 0;
	ib->start_selection = -1;

	ib->normal_font = NULL;
	ib->bold_font = NULL;
	ib->tip = NULL;

	ib->colrow_being_resized = -1;
	ib->has_resize_guides = FALSE;
	ib->pango.item = pango_item_new ();
	ib->pango.glyphs = pango_glyph_string_new ();
}

static void
item_bar_class_init (GObjectClass  *gobject_klass)
{
	FooCanvasItemClass *item_klass = (FooCanvasItemClass *) gobject_klass;

	parent_class = g_type_class_peek_parent (gobject_klass);

	gobject_klass->dispose = item_bar_dispose;
	gobject_klass->set_property = item_bar_set_property;
	g_object_class_install_property (gobject_klass, ITEM_BAR_PROP_PANE,
		g_param_spec_object ("pane", "pane",
			"The pane containing the associated grid",
			GNM_PANE_TYPE,
			GSF_PARAM_STATIC | G_PARAM_WRITABLE));
	g_object_class_install_property (gobject_klass, ITEM_BAR_PROP_IS_COL_HEADER,
		g_param_spec_boolean ("IsColHeader", "IsColHeader",
			"Is the item-bar a header for columns or rows",
			FALSE,
			GSF_PARAM_STATIC | G_PARAM_WRITABLE));

	item_klass->update      = item_bar_update;
	item_klass->realize     = item_bar_realize;
	item_klass->unrealize   = item_bar_unrealize;
	item_klass->draw        = item_bar_draw;
	item_klass->point       = item_bar_point;
	item_klass->event       = item_bar_event;
}

GSF_CLASS (ItemBar, item_bar,
	   item_bar_class_init, item_bar_init,
	   FOO_TYPE_CANVAS_ITEM);

/* vim: set sw=8: */
/*
 * A canvas item implementing row/col headers with support for outlining.
 *
 * Author:
 *     Miguel de Icaza (miguel@kernel.org)
 *     Jody Goldberg   (jody@gnome.org)
 */

#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include "gnumeric.h"
#include "item-bar.h"

#include "style-color.h"
#include "sheet.h"
#include "sheet-control-gui-priv.h"
#include "application.h"
#include "selection.h"
#include "gnumeric-canvas.h"
#include "workbook-edit.h"
#include "gui-util.h"
#include "parse-util.h"
#include "commands.h"

#include <gsf/gsf-impl-utils.h>
#include <gtk/gtklabel.h>
#define GNUMERIC_ITEM "BAR"
#include "item-debug.h"

#include <string.h>

struct _ItemBar {
	FooCanvasItem  canvas_item;

	GnmCanvas	*gcanvas;
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
	ITEM_BAR_PROP_GNUMERIC_CANVAS,
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
 *
 * Scale fonts and sizes by the pixels_per_unit of the associated sheet.
 *
 * returns : the size of the fixed dimension.
 */
int
item_bar_calc_size (ItemBar *ib)
{
	SheetControlGUI	* const scg = ib->gcanvas->simple.scg;
	Sheet const *sheet = ((SheetControl *) scg)->sheet;
	double const zoom_factor = sheet->last_zoom_factor_used;
	PangoContext *context;
	const PangoFontDescription *src_desc = wbcg_get_font_desc (scg->wbcg);
	PangoFontDescription *desc;
	PangoLanguage *language;
	PangoFontMetrics *metrics;
	int size = pango_font_description_get_size (src_desc);

	ib_fonts_unref (ib);

	context = gtk_widget_get_pango_context (GTK_WIDGET (ib->gcanvas));
	language = pango_language_from_string ("C");
	desc = pango_font_description_copy (src_desc);
	pango_font_description_set_size (desc, zoom_factor * size);

	ib->normal_font = pango_context_load_font (context, desc);
	metrics = pango_font_get_metrics (ib->normal_font, language);
	ib->normal_font_ascent = PANGO_PIXELS (pango_font_metrics_get_ascent (metrics));
	pango_font_metrics_unref (metrics);

	pango_font_description_set_weight (desc, PANGO_WEIGHT_BOLD);
	ib->bold_font = pango_context_load_font (context, desc);
	metrics = pango_font_get_metrics (ib->bold_font, language);
	ib->bold_font_ascent = PANGO_PIXELS (pango_font_metrics_get_ascent (metrics));

	/*
	 * Use the size of the bold header font to size the free dimensions.
	 * Add 2 pixels above and below
	 */
	ib->cell_height = 2 + 2 +
		PANGO_PIXELS (pango_font_metrics_get_ascent (metrics) +
			      pango_font_metrics_get_descent (metrics));

	/* 5 pixels left and right plus the width of the widest string I can think of */
	if (ib->is_col_header) {
		static const char Ws[10 + 1] = "WWWWWWWWWW";
		int labellen = strlen (col_name (SHEET_MAX_COLS - 1));
		ib->cell_width = gnm_measure_string (context, desc,
						     Ws + (10 - labellen));
	} else {
		static const char eights[10 + 1] = "8888888888";
		int labellen = strlen (row_name (SHEET_MAX_ROWS - 1));
		ib->cell_width = gnm_measure_string (context, desc,
						     eights + (10 - labellen));
	}
	ib->cell_width += 5 + 5;

	pango_font_metrics_unref (metrics);
	pango_font_description_free (desc);

	ib->pango.item->analysis.font = g_object_ref (ib->normal_font);
	ib->pango.item->analysis.shape_engine =
		pango_font_find_shaper (ib->normal_font, language, 'A');

	ib->indent = ib_compute_pixels_from_indent (sheet, ib->is_col_header);
	foo_canvas_item_request_update (FOO_CANVAS_ITEM (ib));

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
		item->x2 = INT_MAX/2;
		item->y2 = (ib->cell_height + ib->indent);
	} else {
		item->x2 = (ib->cell_width  + ib->indent);
		item->y2 = INT_MAX/2;
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
	GtkWidget	*canvas = GTK_WIDGET (FOO_CANVAS_ITEM (ib)->canvas);
	GdkGC 		*gc;
	PangoFont	*font;
	PangoRectangle   size;
	int shadow, ascent;

	switch (type) {
	default:
	case COL_ROW_NO_SELECTION:
		shadow = GTK_SHADOW_OUT;
		gc = canvas->style->bg_gc [GTK_STATE_ACTIVE];
		font = ib->normal_font;
		ascent = ib->normal_font_ascent;
		break;

	case COL_ROW_PARTIAL_SELECTION:
		shadow = GTK_SHADOW_OUT;
		gc = canvas->style->dark_gc [GTK_STATE_PRELIGHT];
		font = ib->bold_font;
		ascent = ib->bold_font_ascent;
		break;

	case COL_ROW_FULL_SELECTION:
		shadow = GTK_SHADOW_IN;
		gc = canvas->style->dark_gc [GTK_STATE_NORMAL];
		font = ib->bold_font;
		ascent = ib->bold_font_ascent;
		break;
	}
	g_return_if_fail (font != NULL);

	gdk_draw_rectangle (drawable, gc, TRUE,
			    rect->x + 1, rect->y + 1, rect->width-2, rect->height-2);
	gtk_paint_shadow (canvas->style, drawable, GTK_STATE_NORMAL, shadow,
			  NULL, NULL, "GnmItemBarCell",
			  rect->x, rect->y, rect->width, rect->height);
	gdk_gc_set_clip_rectangle (text_gc, rect);

	g_object_unref (ib->pango.item->analysis.font);
	ib->pango.item->analysis.font = g_object_ref (font);
	pango_shape (str, strlen (str), &(ib->pango.item->analysis), ib->pango.glyphs);
	pango_glyph_string_extents (ib->pango.glyphs, font, NULL, &size);
	gdk_draw_glyphs (drawable, text_gc, font,
		rect->x + (rect->width - PANGO_PIXELS (size.width)) / 2,
		rect->y + (rect->height - PANGO_PIXELS (size.height)) / 2 + ascent + 1,
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
	GnmCanvas const	      *gcanvas = ib->gcanvas;
	SheetControlGUI const *scg    = gcanvas->simple.scg;
	Sheet const           *sheet  = ((SheetControl *) scg)->sheet;
	SheetView const	      *sv     = ((SheetControl *) scg)->view;
	GtkWidget *canvas = GTK_WIDGET (FOO_CANVAS_ITEM (item)->canvas);
	ColRowInfo const *cri, *next = NULL;
	int pixels;
	gboolean prev_visible;
	gboolean const draw_below = sheet->outline_symbols_below;
	gboolean const draw_right = sheet->outline_symbols_right;
	int prev_level;
	GdkRectangle rect;
	GdkPoint points[3];
	gboolean has_object = scg->new_object != NULL || scg->current_object != NULL;
	int shadow;

	if (ib->is_col_header) {
		int const inc = item_bar_group_size (ib, sheet->cols.max_outline_level);
		int const base_pos = .2 * inc;
		int const len = (inc > 4) ? 4 : inc;
		int const end = expose->area.x + expose->area.width;

		/* See comment above for explaination of the extra 1 pixel */
		int total = 1 + gcanvas->first_offset.col;
		int col = gcanvas->first.col;

		rect.y = ib->indent;
		rect.height = ib->cell_height;

		/* 
		 * See comment below for explanation of this. 
		 * shadow type selection must be keep in sync with code in ib_draw_cell.
		 */
		if (col > 0 && !has_object && sv_selection_col_type (sv, col-1) == COL_ROW_FULL_SELECTION) 
			shadow = GTK_SHADOW_IN;
		else  
			shadow = GTK_SHADOW_OUT;
		gtk_paint_shadow (canvas->style, drawable, GTK_STATE_NORMAL, shadow,
				  NULL, NULL, "GnmItemBarCell",
				  total-10, rect.y, 10, rect.height);

		if (col > 0) {
			cri = sheet_col_get_info (sheet, col-1);
			prev_visible = cri->visible;
			prev_level = cri->outline_level;
		} else {
			prev_visible = TRUE;
			prev_level = 0;
		}

		do {
			if (col >= SHEET_MAX_COLS)
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
				int level, i = 0, pos = base_pos;
				int left = total;

				total += pixels;
				rect.x = left;
				rect.width = pixels;
				ib_draw_cell (ib, drawable, ib->text_gc,
					       has_object ? COL_ROW_NO_SELECTION
					       : sv_selection_col_type (sv, col),
					       col_name (col), &rect);

				if (len > 0) {
					if (!draw_right) {
						next = sheet_col_get_info (sheet, col + 1);
						points[0].x = left;
					} else
						points[0].x = total;

					/* draw the start or end marks and the vertical lines */
					points[1].x = points[2].x = left + pixels/2;
					for (level = cri->outline_level; i++ < level ; pos += inc) {
						points[0].y = points[1].y = pos;
						points[2].y		  = pos + len;
						if (draw_right && i > prev_level)
							gdk_draw_lines (drawable, ib->lines, points, 3);
						else if (!draw_right && i > next->outline_level)
							gdk_draw_lines (drawable, ib->lines, points, 3);
						else
							gdk_draw_line (drawable, ib->lines,
								       left, pos, total, pos);
					}

					if (draw_right) {
						if (prev_level > level) {
							int safety = 0;
							int top = pos - base_pos;
							int size = inc < pixels ? inc : pixels;

							if (size > 15)
								size = 15;
							else if (size < 6)
								safety = 6 - size;

							top += 2; /* inside cell's shadow */
							gtk_paint_shadow
								(canvas->style, drawable,
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
						if (next->outline_level > level) {
							int safety = 0;
							int top = pos - base_pos;
							int size = inc < pixels ? inc : pixels;
							int right;

							if (size > 15)
								size = 15;
							else if (size < 6)
								safety = 6 - size;

							right = total - size;
							top += 2; /* inside cell's shadow */
							gtk_paint_shadow
								(canvas->style, drawable,
								 GTK_STATE_NORMAL,
								 next->visible ? GTK_SHADOW_OUT : GTK_SHADOW_IN,
								 NULL, NULL, "GnmItemBarCell",
								 right, top+safety, size, size);
							if (size > 9) {
								if (!next->visible) {
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
		} while (total <= end);
	} else {
		int const inc = item_bar_group_size (ib, sheet->rows.max_outline_level);
		int const base_pos = .2 * inc;
		int const len = (inc > 4) ? 4 : inc;
		int const end = expose->area.y + expose->area.height;

		int total = 1 + gcanvas->first_offset.row;
		int row = gcanvas->first.row;

		rect.x = ib->indent;
		rect.width = ib->cell_width;

		/* To avoid overlaping the cells the shared pixel belongs to
		 * the cell above.  This has the nice property that the bottom
		 * dark line of the shadow aligns with the grid lines.
		 * Unfortunately it also implies a 1 pixel empty space at the
		 * top of the bar.  Which we are forced to fill in with
		 * something.  For now we draw a shadow there to be compatible 
		 * with the colour used on the bottom of the cell shadows.
		 *
		 * shadow type selection must be keep in sync with code in ib_draw_cell.
		 */
		if (row > 0 && !has_object && sv_selection_row_type (sv, row - 1) == COL_ROW_FULL_SELECTION)
			shadow = GTK_SHADOW_IN;
		else 
			shadow = GTK_SHADOW_OUT;
		gtk_paint_shadow (canvas->style, drawable, GTK_STATE_NORMAL, shadow,
				  NULL, NULL, "GnmItemBarCell",
				  rect.x, total-10, rect.width, 10);

		if (row > 0) {
			cri = sheet_row_get_info (sheet, row-1);
			prev_visible = cri->visible;
			prev_level = cri->outline_level;
		} else {
			prev_visible = TRUE;
			prev_level = 0;
		}

		do {
			if (row >= SHEET_MAX_ROWS)
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
					for (level = cri->outline_level; i++ < level ; pos += inc) {
						points[0].x = points[1].x = pos;
						points[2].x		  = pos + len;
						if (draw_below && i > prev_level)
							gdk_draw_lines (drawable, ib->lines, points, 3);
						else if (!draw_below && i > next->outline_level)
							gdk_draw_lines (drawable, ib->lines, points, 3);
						else
							gdk_draw_line (drawable, ib->lines,
								       pos, top, pos, total);
					}

					if (draw_below) {
						if (prev_level > level) {
							int safety = 0;
							int left = pos - base_pos;
							int size = inc < pixels ? inc : pixels;

							if (size > 15)
								size = 15;
							else if (size < 6)
								safety = 6 - size;

							top += 2; /* inside cell's shadow */
							gtk_paint_shadow
								(canvas->style, drawable,
								 GTK_STATE_NORMAL,
								 prev_visible ? GTK_SHADOW_OUT : GTK_SHADOW_IN,
								 NULL, NULL, "GnmItemBarCell",
								 left+safety, top, size, size);
							if (size > 9) {
								if (!prev_visible) {
									left++;
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
							int safety = 0;
							int left = pos - base_pos;
							int size = inc < pixels ? inc : pixels;
							int bottom;

							if (size > 15)
								size = 15;
							else if (size < 6)
								safety = 6 - size;

							bottom = total - size;
							top += 2; /* inside cell's shadow */
							gtk_paint_shadow
								(canvas->style, drawable,
								 GTK_STATE_NORMAL,
								 next->visible ? GTK_SHADOW_OUT : GTK_SHADOW_IN,
								 NULL, NULL, "GnmItemBarCell",
								 left+safety, bottom, size, size);
							if (size > 9) {
								if (!next->visible) {
									left++;
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
 *
 * NOTE : this could easily be optimized.  We need not start at 0 every time.
 *        We could potentially use the routines in gnumeric-canvas.
 *
 * return -1 if the event is outside the item boundary
 */
static ColRowInfo const *
is_pointer_on_division (ItemBar const *ib, int pos, int *the_total, int *the_element)
{
	Sheet *sheet = sc_sheet (SHEET_CONTROL (ib->gcanvas->simple.scg));
	ColRowInfo const *cri;
	int i, total = 0;

	for (i = 0; total < pos; i++) {
		if (ib->is_col_header) {
			if (i >= SHEET_MAX_COLS) {
				if (the_element)
					*the_element = -1;
				return NULL;
			}
			cri = sheet_col_get_info (sheet, i);
		} else {
			if (i >= SHEET_MAX_ROWS) {
				if (the_element)
					*the_element = -1;
				return NULL;
			}
			cri = sheet_row_get_info (sheet, i);
		}

		if (cri->visible) {
			total += cri->size_pixels;

			if (wbcg_edit_get_guru (ib->gcanvas->simple.scg->wbcg) == NULL &&
			    !wbcg_is_editing (ib->gcanvas->simple.scg->wbcg) &&
			    (total - 4 < pos) && (pos < total + 4)) {
				if (the_total)
					*the_total = total;
				if (the_element)
					*the_element = i;

				return cri;
			}
		}

		if (total > pos) {
			if (the_element)
				*the_element = i;
			return NULL;
		}
	}
	if (the_element)
		*the_element = -1;
	return NULL;
}

static void
ib_set_cursor (ItemBar *ib, int x, int y)
{
	GtkWidget *canvas = GTK_WIDGET (FOO_CANVAS_ITEM (ib)->canvas);
	GdkCursor *cursor = ib->normal_cursor;
	int major, minor;

	/* We might be invoked before we are realized */
	if (!canvas->window)
		return;

	if (ib->is_col_header) {
		major = x;
		minor = y;
	} else {
		major = y;
		minor = x;
	}

	if (minor >= ib->indent &&
	    is_pointer_on_division (ib, major, NULL, NULL) != NULL)
		cursor = ib->change_cursor;

	gdk_window_set_cursor (canvas->window, cursor);
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
		scg_colrow_size_set (ib->gcanvas->simple.scg,
				     ib->is_col_header,
				     ib->colrow_being_resized, new_size);
	ib->colrow_being_resized = -1;
	ib->has_resize_guides = FALSE;
	scg_colrow_resize_stop (ib->gcanvas->simple.scg);

	if (ib->tip != NULL) {
		gtk_widget_destroy (gtk_widget_get_toplevel (ib->tip));
		ib->tip = NULL;
	}
}

static gboolean
cb_extend_selection (GnmCanvas *gcanvas,
		     int col, int row, gpointer user_data)
{
	ItemBar * const ib = user_data;
	gboolean const is_cols = ib->is_col_header;
	scg_colrow_select (gcanvas->simple.scg,
			   is_cols, is_cols ? col : row, GDK_SHIFT_MASK);
	return TRUE;
}

static gint
outline_button_press (ItemBar const *ib, int element, int pixel)
{
	SheetControl *sc = (SheetControl *) ib->gcanvas->simple.scg;
	Sheet * const sheet = sc->sheet;
	int inc, step;

	if (ib->is_col_header) {
		if (sheet->cols.max_outline_level <= 0)
			return TRUE;
		inc = (ib->indent - 2) / (sheet->cols.max_outline_level + 1);
	} else if (sheet->rows.max_outline_level > 0)
		inc = (ib->indent - 2) / (sheet->rows.max_outline_level + 1);
	else
		return TRUE;

	step = pixel / inc;

	cmd_selection_outline_change (sc->wbc, ib->is_col_header, element, step);
	return TRUE;
}

static gint
item_bar_event (FooCanvasItem *item, GdkEvent *e)
{
	ColRowInfo const *cri;
	FooCanvas	* const canvas = item->canvas;
	ItemBar		* const ib = ITEM_BAR (item);
	GnmCanvas	* const gcanvas = ib->gcanvas;
	SheetControlGUI	* const scg = gcanvas->simple.scg;
	SheetControl	* const sc = (SheetControl *) gcanvas->simple.scg;
	Sheet		* const sheet = sc->sheet;
	WorkbookControlGUI * const wbcg = scg->wbcg;
	gboolean const is_cols = ib->is_col_header;
	int pos, other_pos, start, element, x, y;

	/* NOTE :
	 * No need to map coordinates since we do the zooming of the item bars manually
	 * there is no transform needed.
	 */
	switch (e->type){
	case GDK_ENTER_NOTIFY:
		foo_canvas_w2c (canvas, e->crossing.x, e->crossing.y, &x, &y);
		ib_set_cursor (ib, x, y);
		break;

	case GDK_MOTION_NOTIFY:
		foo_canvas_w2c (canvas, e->motion.x, e->motion.y, &x, &y);

		/* Do col/row resizing or incremental marking */
		if (ib->colrow_being_resized != -1) {
			int new_size;
			if (!ib->has_resize_guides) {
				ib->has_resize_guides = TRUE;
				scg_colrow_resize_start	(ib->gcanvas->simple.scg,
							 ib->is_col_header,
							 ib->colrow_being_resized);

				gnm_simple_canvas_grab (item,
					GDK_POINTER_MOTION_MASK |
					GDK_BUTTON_RELEASE_MASK,
					ib->change_cursor,
					e->motion.time);
			}

			pos = (is_cols) ? x : y;
			new_size = pos - ib->resize_start_pos;
			cri = sheet_colrow_get_info (sheet,
				ib->colrow_being_resized, is_cols);

			/* Ensure we always have enough room for the margins */
			if (new_size <= (cri->margin_a + cri->margin_b)) {
				new_size = cri->margin_a + cri->margin_b + 1;
				if (is_cols)
					pos = gcanvas->first_offset.col +
						scg_colrow_distance_get (scg, TRUE,
							gcanvas->first.col,
							ib->colrow_being_resized);
				else
					pos = gcanvas->first_offset.row +
						scg_colrow_distance_get (scg, FALSE,
							gcanvas->first.row,
							ib->colrow_being_resized);
				pos += new_size;
			}

			ib->colrow_resize_size = new_size;
			colrow_tip_setlabel (ib, is_cols, new_size);
			scg_colrow_resize_move (scg, is_cols, pos);

			/* Redraw the ItemBar to show nice incremental progress */
			foo_canvas_request_redraw (canvas, 0, 0, INT_MAX/2, INT_MAX/2);

		} else if (ib->start_selection != -1) {

			gnm_canvas_handle_motion (ib->gcanvas,
				canvas, &e->motion,
				GNM_CANVAS_SLIDE_AT_COLROW_BOUND |
					(is_cols ? GNM_CANVAS_SLIDE_X : GNM_CANVAS_SLIDE_Y),
				cb_extend_selection, ib);
		} else
			ib_set_cursor (ib, x, y);
		break;

	case GDK_BUTTON_PRESS:
		/* Ignore scroll wheel events */
		if (e->button.button > 3)
			return FALSE;

		if (wbcg_edit_get_guru (wbcg) == NULL)
			scg_mode_edit (sc);

		foo_canvas_w2c (canvas, e->button.x, e->button.y, &x, &y);
		if (is_cols) {
			pos = x;
			other_pos = y;
		} else {
			pos = y;
			other_pos = x;
		}
		cri = is_pointer_on_division (ib, pos, &start, &element);
		if (element < 0)
			return FALSE;
		if (other_pos < ib->indent)
			return outline_button_press (ib, element, other_pos);

		if (e->button.button == 3) {
			if (wbcg_edit_get_guru (wbcg) != NULL)
				return TRUE;
			/* If the selection does not contain the current row/col
			 * then clear the selection and add it.
			 */
			if (!sv_is_colrow_selected (sc->view, element, is_cols))
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
			ib->resize_start_pos = start - cri->size_pixels;
			ib->colrow_resize_size = cri->size_pixels;

			if (ib->tip == NULL) {
				ib->tip = gnumeric_create_tooltip ();
				colrow_tip_setlabel (ib, is_cols, ib->colrow_resize_size);
				gnumeric_position_tooltip (ib->tip, is_cols);
				gtk_widget_show_all (gtk_widget_get_toplevel (ib->tip));
			}
		} else {
			if (wbcg_edit_get_guru (wbcg) != NULL &&
			    !wbcg_edit_entry_redirect_p (wbcg))
				break;

			/* If we're editing it is possible for this to fail */
			if (!scg_colrow_select (scg, is_cols, element, e->button.state))
				break;

			ib->start_selection = element;
			gnm_canvas_slide_init (gcanvas);
			gnm_simple_canvas_grab (item,
				GDK_POINTER_MOTION_MASK |
				GDK_BUTTON_RELEASE_MASK,
				ib->normal_cursor,
				e->button.time);

		}
		break;

	case GDK_2BUTTON_PRESS: {
		/* Ignore scroll wheel events */
		if (e->button.button > 3)
			return FALSE;

		if (e->button.button != 3)
			item_bar_resize_stop (ib, -1);
		break;
	}

	case GDK_BUTTON_RELEASE: {
		gboolean needs_ungrab = FALSE;

		/* Ignore scroll wheel events */
		if (e->button.button > 3)
			return FALSE;

		gnm_canvas_slide_stop (ib->gcanvas);

		if (ib->start_selection >= 0) {
			needs_ungrab = TRUE;
			ib->start_selection = -1;
			gnm_expr_entry_signal_update (
				wbcg_get_entry_logical (scg->wbcg), TRUE);
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
	case ITEM_BAR_PROP_GNUMERIC_CANVAS:
		ib->gcanvas = g_value_get_object (value);
		break;
	case ITEM_BAR_PROP_IS_COL_HEADER:
		ib->is_col_header = g_value_get_boolean (value);
		break;
	}
	foo_canvas_item_request_update (FOO_CANVAS_ITEM (ib));
}

static void
item_bar_finalize (GObject *obj)
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

	(*G_OBJECT_CLASS (parent_class)->finalize) (obj);
}

static void
item_bar_init (ItemBar *ib)
{
	FooCanvasItem *item = FOO_CANVAS_ITEM (ib);

	item->x1 = 0;
	item->y1 = 0;
	item->x2 = 0;
	item->y2 = 0;

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
	ib->pango.item   = pango_item_new ();
	ib->pango.glyphs = pango_glyph_string_new ();
}

static void
item_bar_class_init (GObjectClass  *gobject_klass)
{
	FooCanvasItemClass *item_klass = (FooCanvasItemClass *) gobject_klass;

	parent_class = g_type_class_peek_parent (gobject_klass);

	gobject_klass->finalize     = item_bar_finalize;
	gobject_klass->set_property = item_bar_set_property;
	g_object_class_install_property (gobject_klass, ITEM_BAR_PROP_GNUMERIC_CANVAS,
		g_param_spec_object ("GnumericCanvas", "GnumericCanvas",
			"the canvas of the associated grid",
			GNM_CANVAS_TYPE, G_PARAM_WRITABLE));
	g_object_class_install_property (gobject_klass, ITEM_BAR_PROP_IS_COL_HEADER,
		g_param_spec_boolean ("IsColHeader", "IsColHeader",
			"Is the item-bar a header for columns or rows",
			FALSE,
			G_PARAM_WRITABLE));

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

/*
 * A canvas item implementing row/col headers with support for outlining.
 *
 * Author:
 *     Miguel de Icaza (miguel@kernel.org)
 *     Jody Goldberg   (jody@gnome.org)
 */

#include <gnumeric-config.h>
#include <gnm-i18n.h>
#include <gnumeric.h>
#include <item-bar.h>
#include <gnm-pane-impl.h>

#include <style-color.h>
#include <sheet.h>
#include <sheet-control-gui.h>
#include <sheet-control-gui-priv.h>
#include <application.h>
#include <selection.h>
#include <wbc-gtk-impl.h>
#include <gui-util.h>
#include <parse-util.h>
#include <commands.h>
#include <gutils.h>

#include <goffice/goffice.h>
#include <gsf/gsf-impl-utils.h>
#define GNUMERIC_ITEM "BAR"

#include <string.h>

struct _GnmItemBar {
	GocItem	 base;

	GnmPane		*pane;
	GdkCursor       *normal_cursor;
	GdkCursor       *change_cursor;
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

	/* Style: */

	/* [ColRowSelectionType] */
	GdkRGBA selection_colors[3];
	PangoFont *selection_fonts[3];
	int selection_font_ascents[3];
	PangoRectangle selection_logical_sizes[3];
	GtkStyleContext *styles[3];

	GdkRGBA grouping_color;

	GtkBorder padding;
};

typedef GocItemClass GnmItemBarClass;
static GocItemClass *parent_class;

enum {
	GNM_ITEM_BAR_PROP_0,
	GNM_ITEM_BAR_PROP_PANE,
	GNM_ITEM_BAR_PROP_IS_COL_HEADER
};

static int
ib_compute_pixels_from_indent (GnmItemBar *ib, Sheet const *sheet)
{
	gboolean is_cols = ib->is_col_header;
	double const scale =
		sheet->last_zoom_factor_used *
		gnm_app_display_dpi_get (is_cols) / 72.;
	int const indent = is_cols
		? sheet->cols.max_outline_level
		: sheet->rows.max_outline_level;
	if (!sheet->display_outlines || indent <= 0)
		return 0;
	return (int)(ib->padding.left + (indent + 1) * 14 * scale + 0.5);
}

static void
ib_dispose_fonts (GnmItemBar *ib)
{
	unsigned ui;

	for (ui = 0; ui < G_N_ELEMENTS (ib->selection_fonts); ui++)
		g_clear_object (&ib->selection_fonts[ui]);
}

static const GtkStateFlags selection_type_flags[3] = {
	GTK_STATE_FLAG_NORMAL,
	GTK_STATE_FLAG_PRELIGHT,
	GTK_STATE_FLAG_ACTIVE
};

static const char * const selection_styles[3] = {
	// Caution!  assuming a fixed prefix
	"button.itembar",
	"button.itembar:hover",
	"button.itembar:active"
};

static void
ib_reload_color_style (GnmItemBar *ib)
{
	GocItem *item = GOC_ITEM (ib);
	GtkStyleContext *context = goc_item_get_style_context (item);
	unsigned ui;

	gnm_style_context_get_color (context, GTK_STATE_FLAG_NORMAL,
				     &ib->grouping_color);
	gnm_css_debug_color ("item-bar.grouping-color", &ib->grouping_color);

	for (ui = 0; ui < G_N_ELEMENTS (selection_type_flags); ui++) {
		GtkStateFlags state = selection_type_flags[ui];
		gnm_style_context_get_color
			(context, state, &ib->selection_colors[ui]);
		if (gnm_debug_flag ("css")) {
			char *name = g_strdup_printf
				("itembar.%s%s.color",
				 ib->is_col_header ? "col" : "row",
				 selection_styles[ui] + strlen (selection_styles[0]));
			gnm_css_debug_color (name, &ib->selection_colors[ui]);
			g_free (name);
		}
	}
}


static void
ib_reload_sizing_style (GnmItemBar *ib)
{
	GocItem *item = GOC_ITEM (ib);
	SheetControlGUI	* const scg = ib->pane->simple.scg;
	Sheet const *sheet = scg_sheet (scg);
	double const zoom_factor = sheet->last_zoom_factor_used;
	gboolean const char_label =
		ib->is_col_header && !sheet->convs->r1c1_addresses;
	unsigned ui;
	PangoContext *pcontext =
		gtk_widget_get_pango_context (GTK_WIDGET (item->canvas));
	PangoLayout *layout = pango_layout_new (pcontext);
	PangoAttrList *attr_list;
	GList *item_list;

	for (ui = 0; ui < G_N_ELEMENTS (selection_type_flags); ui++) {
		GtkStateFlags state = selection_type_flags[ui];
		PangoFontDescription *desc;
		PangoRectangle ink_rect;
		const char *long_name;
		GtkStyleContext *context;

		g_clear_object (&ib->styles[ui]);
#if GTK_CHECK_VERSION(3,20,0)
		context = go_style_context_from_selector (NULL, selection_styles[ui]);
#else
		context = g_object_ref (goc_item_get_style_context (item));
#endif

		ib->styles[ui] = context;
		gtk_style_context_save (context);
#if !GTK_CHECK_VERSION(3,20,0)
		gtk_style_context_set_state (context, state);
#endif
		gtk_style_context_get (context, state, "font", &desc, NULL);
		pango_font_description_set_size (desc,
						 zoom_factor * pango_font_description_get_size (desc));
		ib->selection_fonts[ui] =
			pango_context_load_font (pcontext, desc);
		if (!ib->selection_fonts[ui]) {
			/* Fallback. */
			pango_font_description_set_family (desc, "Sans");
			ib->selection_fonts[ui] =
				pango_context_load_font (pcontext, desc);
		}

		/*
		 * Figure out how tall the label can be.
		 * (Note that we avoid J/Q/Y which may go below the line.)
		 */
		pango_layout_set_text (layout,
				       char_label ? "AHW" : "0123456789",
				       -1);
		pango_layout_set_font_description (layout, desc);
		pango_font_description_free (desc);
		pango_layout_get_extents (layout, &ink_rect, NULL);
		ib->selection_font_ascents[ui] =
			PANGO_PIXELS (ink_rect.height + ink_rect.y);

		/* The width of the widest string I can think of + padding */
		if (ib->is_col_header) {
			int last = gnm_sheet_get_last_col (sheet);
			long_name = char_label ? col_name (last) :  /* HACK: */row_name (last);
		} else
			long_name = row_name (gnm_sheet_get_last_row (sheet));
		pango_layout_set_text
			(layout,
			 char_label ? "WWWWWWWWWW" : "8888888888",
			 strlen (long_name));
		pango_layout_get_extents (layout, NULL,
					  &ib->selection_logical_sizes[ui]);

		if (state == GTK_STATE_FLAG_NORMAL)
			gtk_style_context_get_padding (context, state,
						       &ib->padding);

		gtk_style_context_restore (context);
	}

	attr_list = pango_attr_list_new ();
	item_list = pango_itemize (pcontext, "A", 0, 1, attr_list, NULL);
	pango_attr_list_unref (attr_list);
	if (ib->pango.item)
		pango_item_free (ib->pango.item);
	ib->pango.item = item_list->data;
	item_list->data = NULL;
	if (item_list->next != NULL)
		g_warning ("Leaking pango items");
	g_list_free (item_list);

	g_object_unref (layout);
}

/**
 * gnm_item_bar_calc_size:
 * @ib: #GnmItemBar
 *
 * Scale fonts and sizes by the pixels_per_unit of the associated sheet.
 *
 * Returns : the size of the fixed dimension.
 **/
int
gnm_item_bar_calc_size (GnmItemBar *ib)
{
	SheetControlGUI	* const scg = ib->pane->simple.scg;
	Sheet const *sheet = scg_sheet (scg);
	int size;
	unsigned ui;

	ib_dispose_fonts (ib);
	ib_reload_sizing_style (ib);

	ib->cell_height = 0;
	ib->cell_width = 0;
	for (ui = 0; ui < G_N_ELEMENTS (ib->selection_logical_sizes); ui++) {
		int h = PANGO_PIXELS (ib->selection_logical_sizes[ui].height) +
			(ib->padding.top + ib->padding.bottom);
		int w = PANGO_PIXELS (ib->selection_logical_sizes[ui].width) +
			(ib->padding.left + ib->padding.right);
		ib->cell_height = MAX (ib->cell_height, h);
		ib->cell_width = MAX (ib->cell_width, w);
	}

	size = ib_compute_pixels_from_indent (ib, sheet);
	if (size != ib->indent) {
		ib->indent = size;
		goc_item_bounds_changed (GOC_ITEM (ib));
	}

	return ib->indent +
		(ib->is_col_header ? ib->cell_height : ib->cell_width);
}

/**
 * item_bar_normal_font:
 * @ib: #GnmItemBar
 *
 * Returns: (transfer full): the bar normal font.
 **/
PangoFontDescription *
item_bar_normal_font (GnmItemBar const *ib)
{
	return pango_font_describe (ib->selection_fonts[COL_ROW_NO_SELECTION]);
}

int
gnm_item_bar_indent (GnmItemBar const *ib)
{
	return ib->indent;
}

static void
item_bar_update_bounds (GocItem *item)
{
	GnmItemBar *ib = GNM_ITEM_BAR (item);
	item->x0 = 0;
	item->y0 = 0;
	if (ib->is_col_header) {
		item->x1 = G_MAXINT64/2;
		item->y1 = (ib->cell_height + ib->indent);
	} else {
		item->x1 = (ib->cell_width  + ib->indent);
		item->y1 = G_MAXINT64/2;
	}
}

static void
item_bar_realize (GocItem *item)
{
	GnmItemBar *ib = GNM_ITEM_BAR (item);
	GdkDisplay *display;

	parent_class->realize (item);

	display = gtk_widget_get_display (GTK_WIDGET (item->canvas));
	ib->normal_cursor =
		gdk_cursor_new_for_display (display, GDK_LEFT_PTR);
	ib->change_cursor =
		gdk_cursor_new_for_display (display,
					    ib->is_col_header
					    ? GDK_SB_H_DOUBLE_ARROW
					    : GDK_SB_V_DOUBLE_ARROW);

	ib_reload_color_style (ib);
	gnm_item_bar_calc_size (ib);
}

static void
item_bar_unrealize (GocItem *item)
{
	GnmItemBar *ib = GNM_ITEM_BAR (item);

	g_clear_object (&ib->change_cursor);
	g_clear_object (&ib->normal_cursor);

	parent_class->unrealize (item);
}

static void
ib_draw_cell (GnmItemBar const * const ib, cairo_t *cr,
	      ColRowSelectionType const type,
	      char const * const str, GocRect *rect)
{
	GtkStyleContext *ctxt = ib->styles[type];

	g_return_if_fail ((size_t)type < G_N_ELEMENTS (selection_type_flags));

	cairo_save (cr);

	gtk_style_context_save (ctxt);
#if !GTK_CHECK_VERSION(3,20,0)
	gtk_style_context_set_state (ctxt, selection_type_flags[type]);
#endif
	gtk_render_background (ctxt, cr, rect->x, rect->y,
			       rect->width + 1, rect->height + 1);

	/* When we are really small leave out the shadow and the text */
	if (rect->width >= 2 && rect->height >= 2) {
		PangoRectangle size;
		PangoFont *font = ib->selection_fonts[type];
		int ascent = ib->selection_font_ascents[type];
		int w, h;
		GdkRGBA c;

		g_return_if_fail (font != NULL);
		g_object_unref (ib->pango.item->analysis.font);
		ib->pango.item->analysis.font = g_object_ref (font);
		pango_shape (str, strlen (str),
			     &(ib->pango.item->analysis), ib->pango.glyphs);
		pango_glyph_string_extents (ib->pango.glyphs, font,
					    NULL, &size);

		gtk_render_frame (ctxt, cr, rect->x, rect->y,
				  rect->width + 1, rect->height + 1);
		w = rect->width - (ib->padding.left + ib->padding.right);
		h = rect->height - (ib->padding.top + ib->padding.bottom);
		cairo_rectangle (cr, rect->x + 1, rect->y + 1,
				 rect->width - 2, rect->height - 2);
		cairo_clip (cr);

		gtk_style_context_get_color (ctxt, selection_type_flags[type], &c);
		gdk_cairo_set_source_rgba (cr, &c);

		cairo_translate (cr,
				 rect->x + ib->padding.left +
				 (w - PANGO_PIXELS (size.width)) / 2,
				 rect->y + ib->padding.top + ascent +
				 (h - PANGO_PIXELS (size.height)) / 2);
		pango_cairo_show_glyph_string (cr, font, ib->pango.glyphs);
	}
	gtk_style_context_restore (ctxt);
	cairo_restore (cr);
}

int
gnm_item_bar_group_size (GnmItemBar const *ib, int max_outline)
{
	return max_outline > 0
		? (ib->indent - 2) / (max_outline + 1)
		: 0;
}

static gboolean
item_bar_draw_region (GocItem const *item, cairo_t *cr,
		      double x_0, double y_0, double x_1, double y_1)
{
	double scale = item->canvas->pixels_per_unit;
	int x0, x1, y0, y1;
	GnmItemBar *ib = GNM_ITEM_BAR (item);
	GnmPane	 const	      *pane = ib->pane;
	SheetControlGUI const *scg    = pane->simple.scg;
	Sheet const           *sheet  = scg_sheet (scg);
	SheetView const	      *sv     = scg_view (scg);
	ColRowInfo const *cri, *next = NULL;
	int pixels;
	gboolean prev_visible;
	gboolean const draw_below = sheet->outline_symbols_below != FALSE;
	gboolean const draw_right = sheet->outline_symbols_right != FALSE;
	int prev_level;
	GocRect rect;
	GocPoint points[3];
	gboolean const has_object = scg->wbcg->new_object != NULL || scg->selected_objects != NULL;
	gboolean const rtl = sheet->text_is_rtl != FALSE;
	int shadow;
	int first_line_offset = 1;
	GtkStyleContext *ctxt = goc_item_get_style_context (item);

	gtk_style_context_save (ctxt);
	goc_canvas_c2w (item->canvas, x_0, y_0, &x0, &y0);
	goc_canvas_c2w (item->canvas, x_1, y_1, &x1, &y1);

	if (ib->is_col_header) {
		int const inc = gnm_item_bar_group_size (ib, sheet->cols.max_outline_level);
		int const base_pos = .2 * inc;
		int const len = (inc > 4) ? 4 : inc;
		int end, total, col = pane->first.col;
		gboolean const char_label = !sheet->convs->r1c1_addresses;

		/* shadow type selection must be keep in sync with code in ib_draw_cell */
		goc_canvas_c2w (item->canvas, pane->first_offset.x / scale, 0, &total, NULL);
		end = x1;
		rect.y = ib->indent;
		rect.height = ib->cell_height;
		shadow = (col > 0 && !has_object && sv_selection_col_type (sv, col-1) == COL_ROW_FULL_SELECTION)
			? GTK_SHADOW_IN : GTK_SHADOW_OUT;

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
				return TRUE;

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
				ib_draw_cell (ib, cr,
					       has_object
						       ? COL_ROW_NO_SELECTION
						       : sv_selection_col_type (sv, col),
					       char_label
						       ? col_name (col)
						       : row_name (col),
					       &rect);

				if (len > 0) {
					cairo_save (cr);
					cairo_set_line_width (cr, 2.0);
					cairo_set_line_cap (cr, CAIRO_LINE_CAP_BUTT);
					cairo_set_line_join (cr, CAIRO_LINE_JOIN_MITER);
					gdk_cairo_set_source_rgba (cr, &ib->grouping_color);
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
						if (i > prev_level) {
							cairo_move_to (cr, points[0].x, points[0].y);
							cairo_line_to (cr, points[1].x, points[1].y);
							cairo_line_to (cr, points[2].x, points[2].y);
						} else if (rtl) {
							cairo_move_to (cr, left - first_line_offset, pos);
							cairo_line_to (cr, total + pixels, pos);
						} else {
							cairo_move_to (cr, left - first_line_offset, pos);
							cairo_line_to (cr, total, pos);
						}
					}
					cairo_stroke (cr);
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

							gtk_style_context_set_state (ctxt, prev_visible ?
							                             GTK_STATE_FLAG_NORMAL: GTK_STATE_FLAG_SELECTED);
							gtk_render_frame (ctxt, cr,
								 left, top+safety, size, size);
							if (size > 9) {
								if (!prev_visible) {
									top++;
									left++;
									cairo_move_to (cr, left+size/2, top+3);
									cairo_line_to (cr, left+size/2, top+size-4);
								}
								cairo_move_to (cr, left+3,	    top+size/2);
								cairo_line_to (cr, left+size-4, top+size/2);
							}
						}
						if (level > 0) {
							cairo_move_to (cr, left+pixels/2, pos);
							cairo_line_to (cr, left+pixels/2, pos+len);
						}
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
							gtk_style_context_set_state (ctxt, prev_visible ?
							                             GTK_STATE_FLAG_NORMAL: GTK_STATE_FLAG_SELECTED);
							gtk_render_frame (ctxt, cr,
								 right, top+safety, size, size);
							if (size > 9) {
								if (!prev_visible) {
									top++;
									right++;
									cairo_move_to (cr, right+size/2, top+3);
									cairo_line_to (cr, right+size/2, top+size-4);
								}
								cairo_move_to (cr, right+3,	    top+size/2);
								cairo_line_to (cr, right+size-4, top+size/2);
							}
						} else if (level > 0) {
								cairo_move_to (cr, left+pixels/2, pos);
								cairo_line_to (cr, left+pixels/2, pos+len);
						}
					}
					cairo_stroke (cr);
					cairo_restore (cr);
				}
			}
			prev_visible = cri->visible;
			prev_level = cri->outline_level;
			++col;
		} while ((rtl && end <= total) || (!rtl && total <= end));
	} else {
		int const inc = gnm_item_bar_group_size (ib, sheet->rows.max_outline_level);
		int base_pos = .2 * inc;
		int const len = (inc > 4) ? 4 : inc;
		int const end = y1;
		int const dir = rtl ? -1 : 1;

		int total = pane->first_offset.y - item->canvas->scroll_y1 * scale;
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
				return TRUE;

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
				ib_draw_cell (ib, cr,
					      has_object
						      ? COL_ROW_NO_SELECTION
						      : sv_selection_row_type (sv, row),
					      row_name (row), &rect);

				if (len > 0) {
					cairo_save (cr);
					cairo_set_line_width (cr, 2.0);
					cairo_set_line_cap (cr, CAIRO_LINE_CAP_BUTT);
					cairo_set_line_join (cr, CAIRO_LINE_JOIN_MITER);
					gdk_cairo_set_source_rgba (cr, &ib->grouping_color);
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
						if (draw_below && i > prev_level) {
							cairo_move_to (cr, points[0].x, points[0].y);
							cairo_line_to (cr, points[1].x, points[1].y);
							cairo_line_to (cr, points[2].x, points[2].y);
						} else if (!draw_below && i > next->outline_level) {
							cairo_move_to (cr, points[0].x, points[0].y);
							cairo_line_to (cr, points[1].x, points[1].y);
							cairo_line_to (cr, points[2].x, points[2].y);
						} else {
							cairo_move_to (cr, pos, top - first_line_offset);
							cairo_line_to (cr, pos, total);
						}
					}
					cairo_stroke (cr);
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
							gtk_style_context_set_state (ctxt, prev_visible ?
							                             GTK_STATE_FLAG_NORMAL: GTK_STATE_FLAG_SELECTED);
							gtk_render_frame (ctxt, cr,
								left+safety, top, size, size);
							if (size > 9) {
								if (!prev_visible) {
									left += dir;
									top++;
									cairo_move_to (cr, left+size/2, top+3);
									cairo_line_to (cr, left+size/2, top+size-4);
								}
								cairo_move_to (cr, left+3,	    top+size/2);
								cairo_line_to (cr, left+size-4, top+size/2);
							}
						}
						if (level > 0) {
							cairo_move_to (cr, pos,      top+pixels/2);
							cairo_line_to (cr, pos+len,  top+pixels/2);
						}
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
							gtk_style_context_set_state (ctxt, prev_visible ?
							                             GTK_STATE_FLAG_NORMAL: GTK_STATE_FLAG_SELECTED);
							gtk_render_frame (ctxt,cr,
								 left+safety*dir, bottom, size, size);
							if (size > 9) {
								if (!next->visible) {
									left += dir;
									top++;
									cairo_move_to (cr, left+size/2, bottom+3);
									cairo_line_to (cr, left+size/2, bottom+size-4);
								}
								cairo_move_to (cr, left+3,	    bottom+size/2);
								cairo_line_to (cr, left+size-4, bottom+size/2);
							}
						} else if (level > 0) {
							cairo_move_to (cr, pos,      top+pixels/2);
							cairo_line_to (cr, pos+len,  top+pixels/2);
						}
					}
					cairo_stroke (cr);
					cairo_restore (cr);
				}
			}
			prev_visible = cri->visible;
			prev_level = cri->outline_level;
			++row;
		} while (total <= end);
	}
	gtk_style_context_restore (ctxt);
	return TRUE;
}

static double
item_bar_distance (GocItem *item, double x, double y,
		   GocItem **actual_item)
{
	*actual_item = item;
	return 0.0;
}

/**
 * is_pointer_on_division:
 * @ib: #GnmItemBar
 * @x: in world coords
 * @y: in world coords
 * @the_total:
 * @the_element:
 * @minor_pos:
 *
 * NOTE : this could easily be optimized.  We need not start at 0 every time.
 *        We could potentially use the routines in gnm-pane.
 *
 * Returns non-%NULL if point (@x,@y) is on a division
 **/
static ColRowInfo const *
is_pointer_on_division (GnmItemBar const *ib, gint64 x, gint64 y,
			gint64 *the_total, int *the_element, gint64 *minor_pos)
{
	Sheet *sheet = scg_sheet (ib->pane->simple.scg);
	ColRowInfo const *cri;
	gint64 major, minor, total = 0;
	int i;

	if (ib->is_col_header) {
		major = x;
		minor = y;
		i = ib->pane->first.col;
		total = ib->pane->first_offset.x;
	} else {
		major = y;
		minor = x;
		i = ib->pane->first.row;
		total = ib->pane->first_offset.y;
	}
	if (NULL != minor_pos)
		*minor_pos = minor;
	if (NULL != the_element)
		*the_element = -1;
	for (; total < major; i++) {
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
ib_set_cursor (GnmItemBar *ib, gint64 x, gint64 y)
{
	GdkWindow *window = gtk_widget_get_window (GTK_WIDGET (ib->base.canvas));
	GdkCursor *cursor = ib->normal_cursor;

	/* We might be invoked before we are realized */
	if (NULL == window)
		return;
	if (NULL != is_pointer_on_division (ib, x, y, NULL, NULL, NULL))
		cursor = ib->change_cursor;
	gdk_window_set_cursor (window, cursor);
}

static void
colrow_tip_setlabel (GnmItemBar *ib, gboolean const is_cols, int size_pixels)
{
	if (ib->tip != NULL) {
		char *buffer, *points, *pixels;
		char const *label = is_cols ? _("Width:") : _("Height:");
		double const scale = 72. / gnm_app_display_dpi_get (!is_cols);
		double size_points = scale*size_pixels;

		/* xgettext: This is input to ngettext based on the number of pixels. */
		pixels = g_strdup_printf (ngettext ("(%d pixel)", "(%d pixels)", size_pixels),
					  size_pixels);

		if (size_points == floor (size_points))
			/* xgettext: This is input to ngettext based on the integer number of points. */
			points = g_strdup_printf (ngettext (_("%d.00 pt"), _("%d.00 pts"), (int) gnm_floor (size_points)),
						  (int) gnm_floor (size_points));
		else
			/* xgettext: The number of points here is always a fractional number, ie. not an integer. */
			points = g_strdup_printf (_("%.2f pts"), size_points);

		buffer = g_strconcat (label, " ", points, " ", pixels, NULL);
		g_free (pixels);
		g_free (points);
		gtk_label_set_text (GTK_LABEL (ib->tip), buffer);
		g_free(buffer);
	}
}

static void
item_bar_resize_stop (GnmItemBar *ib, int new_size)
{
	if (ib->colrow_being_resized != -1) {
		if (new_size != 0)
			scg_colrow_size_set (ib->pane->simple.scg,
					     ib->is_col_header,
					     ib->colrow_being_resized,
					     new_size);
		ib->colrow_being_resized = -1;
	}
	if (ib->has_resize_guides) {
		ib->has_resize_guides = FALSE;
		scg_size_guide_stop (ib->pane->simple.scg);
	}
	if (ib->tip != NULL) {
		gtk_widget_destroy (gtk_widget_get_toplevel (ib->tip));
		ib->tip = NULL;
	}
}

static gboolean
cb_extend_selection (GnmPane *pane, GnmPaneSlideInfo const *info)
{
	GnmItemBar * const ib = info->user_data;
	gboolean const is_cols = ib->is_col_header;
	scg_colrow_select (pane->simple.scg,
		is_cols, is_cols ? info->col : info->row, GDK_SHIFT_MASK);
	return TRUE;
}

static gint
outline_button_press (GnmItemBar const *ib, int element, int pixel)
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

static gboolean
item_bar_button_pressed (GocItem *item, int button, double x_, double y_)
{
	ColRowInfo const *cri;
	GocCanvas	* const canvas = item->canvas;
	GnmItemBar		* const ib = GNM_ITEM_BAR (item);
	GnmPane		* const pane = ib->pane;
	SheetControlGUI	* const scg = pane->simple.scg;
	SheetControl	* const sc = (SheetControl *) pane->simple.scg;
	Sheet		* const sheet = sc_sheet (sc);
	WBCGtk * const wbcg = scg_wbcg (scg);
	gboolean const is_cols = ib->is_col_header;
	gint64 minor_pos, start;
	int element;
	GdkEvent *event = goc_canvas_get_cur_event (item->canvas);
	GdkEventButton *bevent = &event->button;
	gint64 x = x_ * item->canvas->pixels_per_unit, y = y_ * item->canvas->pixels_per_unit;

	if (ib->colrow_being_resized != -1 || ib->start_selection != -1) {
		// This happens with repeated clicks on colrow divider.
		// Ignore it.  Definitely don't regrab.
		return TRUE;
	}

	if (button > 3)
		return FALSE;

	if (wbc_gtk_get_guru (wbcg) == NULL)
		scg_mode_edit (scg);

	cri = is_pointer_on_division (ib, x, y,
		&start, &element, &minor_pos);
	if (element < 0)
		return FALSE;
	if (minor_pos < ib->indent)
		return outline_button_press (ib, element, minor_pos);

	if (button == 3) {
		if (wbc_gtk_get_guru (wbcg) != NULL)
			return TRUE;
		/* If the selection does not contain the current row/col
		 * then clear the selection and add it.
		 */
		if (!sv_is_colrow_selected (sc_view (sc), element, is_cols))
			scg_colrow_select (scg, is_cols,
					   element, bevent->state);

		scg_context_menu (scg, event, is_cols, !is_cols);
		return TRUE;
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
			GtkWidget *cw = GTK_WIDGET (canvas);
			int wx, wy;
			ib->tip = gnm_create_tooltip (cw);
			colrow_tip_setlabel (ib, is_cols, ib->colrow_resize_size);
			/* Position above the current point for both
			 * col and row headers.  trying to put it
			 * beside for row headers often ends up pushing
			 * the tip under the cursor which can have odd
			 * effects on the event stream.  win32 was
			 * different from X. */

			gnm_canvas_get_position (canvas, &wx, &wy,x, y);
			gnm_position_tooltip (ib->tip,
						   wx, wy, TRUE);
			gtk_widget_show_all (gtk_widget_get_toplevel (ib->tip));
		}
	} else {
		if (wbc_gtk_get_guru (wbcg) != NULL &&
		    !wbcg_entry_has_logical (wbcg))
			return TRUE;

		/* If we're editing it is possible for this to fail */
		if (!scg_colrow_select (scg, is_cols, element, bevent->state))
			return TRUE;

		ib->start_selection = element;
		gnm_pane_slide_init (pane);
	}
	gnm_simple_canvas_grab (item);
	return TRUE;
}

static gboolean
item_bar_2button_pressed (GocItem *item, int button, double x, double y)
{
	GnmItemBar		* const ib = GNM_ITEM_BAR (item);
	if (button > 3)
		return FALSE;

	if (button != 3)
		item_bar_resize_stop (ib, -1);
	return TRUE;
}

static gboolean
item_bar_enter_notify (GocItem *item, double x_, double y_)
{
	GnmItemBar		* const ib = GNM_ITEM_BAR (item);
	gint64 x = x_ * item->canvas->pixels_per_unit, y = y_ * item->canvas->pixels_per_unit;
	ib_set_cursor (ib, x, y);
	return TRUE;
}

static gboolean
item_bar_motion (GocItem *item, double x_, double y_)
{
	ColRowInfo const *cri;
	GocCanvas	* const canvas = item->canvas;
	GnmItemBar		* const ib = GNM_ITEM_BAR (item);
	GnmPane		* const pane = ib->pane;
	SheetControlGUI	* const scg = pane->simple.scg;
	SheetControl	* const sc = (SheetControl *) pane->simple.scg;
	Sheet		* const sheet = sc_sheet (sc);
	gboolean const is_cols = ib->is_col_header;
	gint64 pos;
	gint64 x = x_ * item->canvas->pixels_per_unit, y = y_ * item->canvas->pixels_per_unit;

	if (ib->colrow_being_resized != -1) {
		int new_size;
		if (!ib->has_resize_guides) {
			ib->has_resize_guides = TRUE;
			scg_size_guide_start (ib->pane->simple.scg,
					      ib->is_col_header,
					      ib->colrow_being_resized,
					      TRUE);
		}

		cri = sheet_colrow_get_info (sheet,
			ib->colrow_being_resized, is_cols);
		pos = is_cols ? x: y;
		new_size = pos - ib->resize_start_pos;
		if (is_cols && sheet->text_is_rtl)
			new_size += cri->size_pixels;

		/* Ensure we always have enough room for the margins */
		if (is_cols) {
			if (new_size <= (GNM_COL_MARGIN + GNM_COL_MARGIN)) {
				new_size = GNM_COL_MARGIN + GNM_COL_MARGIN + 1;
				pos = pane->first_offset.x +
					scg_colrow_distance_get (scg, TRUE,
						pane->first.col,
						ib->colrow_being_resized);
				pos += new_size;
			}
		} else {
			if (new_size <= (GNM_ROW_MARGIN + GNM_ROW_MARGIN)) {
				new_size = GNM_ROW_MARGIN + GNM_ROW_MARGIN + 1;
				pos = pane->first_offset.y +
					scg_colrow_distance_get (scg, FALSE,
						pane->first.row,
						ib->colrow_being_resized);
				pos += new_size;
			}
		}

		ib->colrow_resize_size = new_size;
		colrow_tip_setlabel (ib, is_cols, new_size);
		scg_size_guide_motion (scg, is_cols, pos);

		/* Redraw the GnmItemBar to show nice incremental progress */
		goc_canvas_invalidate (canvas, 0, 0, G_MAXINT/2,  G_MAXINT/2);

	} else if (ib->start_selection != -1) {
		gnm_pane_handle_motion (ib->pane,
			canvas, x, y,
			GNM_PANE_SLIDE_AT_COLROW_BOUND |
				(is_cols ? GNM_PANE_SLIDE_X : GNM_PANE_SLIDE_Y),
			cb_extend_selection, ib);
	} else
		ib_set_cursor (ib, x, y);
	return TRUE;
}

static gboolean
item_bar_button_released (GocItem *item, int button, double x, double y)
{
	GnmItemBar *ib = GNM_ITEM_BAR (item);
	if (item == goc_canvas_get_grabbed_item (item->canvas))
		gnm_simple_canvas_ungrab (item);

	if (ib->colrow_being_resized >= 0) {
		if (ib->has_resize_guides)
			item_bar_resize_stop (ib, ib->colrow_resize_size);
		else
			/*
			 * No need to resize, nothing changed.
			 * This will handle the case of a double click.
			 */
			item_bar_resize_stop (ib, 0);
	}
	ib->start_selection = -1;
	return TRUE;
}

static void
item_bar_set_property (GObject *obj, guint param_id,
		       GValue const *value, GParamSpec *pspec)
{
	GnmItemBar *ib = GNM_ITEM_BAR (obj);

	switch (param_id){
	case GNM_ITEM_BAR_PROP_PANE:
		ib->pane = g_value_get_object (value);
		break;
	case GNM_ITEM_BAR_PROP_IS_COL_HEADER:
		ib->is_col_header = g_value_get_boolean (value);
		goc_item_bounds_changed (GOC_ITEM (obj));
		break;
	}
}

static void
item_bar_dispose (GObject *obj)
{
	GnmItemBar *ib = GNM_ITEM_BAR (obj);
	unsigned ui;

	ib_dispose_fonts (ib);

	if (ib->tip) {
		gtk_widget_destroy (ib->tip);
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
	for (ui = 0; ui < G_N_ELEMENTS(ib->styles); ui++)
		g_clear_object (&ib->styles[ui]);

	G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
gnm_item_bar_init (GnmItemBar *ib)
{
	ib->base.x0 = 0;
	ib->base.y0 = 0;
	ib->base.x1 = 0;
	ib->base.y1 = 0;

	ib->dragging = FALSE;
	ib->is_col_header = FALSE;
	ib->cell_width = ib->cell_height = 1;
	ib->indent = 0;
	ib->start_selection = -1;

	ib->tip = NULL;

	ib->colrow_being_resized = -1;
	ib->has_resize_guides = FALSE;
	ib->pango.item = NULL;
	ib->pango.glyphs = pango_glyph_string_new ();

#if !GTK_CHECK_VERSION(3,20,0)
	/* Style-wise we are a button.  */
	gtk_style_context_add_class
		(goc_item_get_style_context (GOC_ITEM (ib)),
		 GTK_STYLE_CLASS_BUTTON);
#endif
}

static void
gnm_item_bar_class_init (GObjectClass  *gobject_klass)
{
	GocItemClass *item_klass = (GocItemClass *) gobject_klass;

	parent_class = g_type_class_peek_parent (gobject_klass);

	gobject_klass->dispose = item_bar_dispose;
	gobject_klass->set_property = item_bar_set_property;
	g_object_class_install_property (gobject_klass, GNM_ITEM_BAR_PROP_PANE,
		g_param_spec_object ("pane",
				     P_("Pane"),
				     P_("The pane containing the associated grid"),
				     GNM_PANE_TYPE,
				     GSF_PARAM_STATIC | G_PARAM_WRITABLE));
	g_object_class_install_property (gobject_klass, GNM_ITEM_BAR_PROP_IS_COL_HEADER,
		g_param_spec_boolean ("IsColHeader",
				      P_("IsColHeader"),
				      P_("Is the item-bar a header for columns or rows"),
				      FALSE,
				      GSF_PARAM_STATIC | G_PARAM_WRITABLE));

	item_klass->realize     = item_bar_realize;
	item_klass->unrealize   = item_bar_unrealize;
	item_klass->draw_region = item_bar_draw_region;
	item_klass->update_bounds  = item_bar_update_bounds;
	item_klass->distance	= item_bar_distance;
	item_klass->button_pressed = item_bar_button_pressed;
	item_klass->button_released = item_bar_button_released;
	item_klass->button2_pressed = item_bar_2button_pressed;
	item_klass->enter_notify = item_bar_enter_notify;
	item_klass->motion = item_bar_motion;
}

GSF_CLASS (GnmItemBar, gnm_item_bar,
	   gnm_item_bar_class_init, gnm_item_bar_init,
	   GOC_TYPE_ITEM)

/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * print.c: Printing routines for Gnumeric
 *
 * Authors:
 *    Miguel de Icaza (miguel@gnu.org)
 *    Morten Welinder (terra@gnome.org)
 *    Andreas J. Guelzow (aguelzow@pyrshep.ca)
 *
 * Copyright 2007, Andreas J. Guelzow, All Rights Reserved
 *
 * Handles printing of Sheets.
 */
#include <gnumeric-config.h>
#include "print-cell.h"

#include "gnumeric.h"
#include "print.h"

#include "gui-util.h"
#include "sheet-object.h"
#include "sheet-object-impl.h"
#include "selection.h"
#include "workbook.h"
#include "workbook-control.h"
#include "wbc-gtk.h"
#include "command-context.h"
#include "dialogs.h"
#include "gnumeric-gconf.h"
#include "libgnumeric.h"
#include "sheet.h"
#include "value.h"
#include "cellspan.h"
#include "print-info.h"
#include "application.h"
#include "sheet-style.h"
#include "ranges.h"
#include "parse-util.h"
#include "style-font.h"
#include "gnumeric-gconf.h"
#include <goffice/utils/go-font.h>
#include <goffice/utils/go-glib-extras.h>

#include <gtk/gtkprintoperation.h>
#include <gtk/gtktogglebutton.h>
#include <gtk/gtkframe.h>
#include <gtk/gtktable.h>
#include <gtk/gtkradiobutton.h>
#include <gtk/gtkspinbutton.h>
#include <gtk/gtklabel.h>

#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>
#include <glib/gfileutils.h>

#include <unistd.h>
#include <errno.h>

/*  The following structure is used by the printing system */

typedef struct {
	GList *gnmSheets;
	GList *gnmSheetRanges;
	Workbook *wb;
	WorkbookControl *wbc;
	Sheet *sheet;
	GtkWidget *button_all_sheets, *button_selected_sheet,
		*button_spec_sheets;
	GtkWidget *button_selection, *button_ignore_printarea;
	GtkWidget *spin_from, *spin_to;
	PrintRange pr;
	guint to, from;
	guint last_pagination;
	HFRenderInfo *hfi;
} PrintingInstance;

typedef struct {
	Sheet *sheet;
	gboolean selection;
        gboolean ignore_printarea;
} SheetPrintInfo;

typedef struct {
	Sheet *sheet;
	GnmRange  range;
	gint n_rep_cols;
	gint n_rep_rows;
	gint first_rep_cols;
	gint first_rep_rows;
} SheetPageRange;

typedef struct {
	gint rc;
	gint count;
	gint first_rep;
	gint n_rep;
} PaginationInfo;


gboolean gnm_print_debug = FALSE;

static PrintingInstance *
printing_instance_new (void)
{
	PrintingInstance * pi = g_new0 (PrintingInstance,1);
	pi->hfi = hf_render_info_new ();

	return pi;
}

static void
printing_instance_delete (PrintingInstance *pi)
{
	go_list_free_custom (pi->gnmSheets, g_free);
	go_list_free_custom (pi->gnmSheetRanges, g_free);
	hf_render_info_destroy (pi->hfi);
	g_free (pi);
}

static void
print_sheet_objects (GtkPrintContext   *context,
		     cairo_t *cr,
		     Sheet const *sheet,
		     GnmRange *range,
		     double base_x, double base_y)
{
	GSList *ptr, *objects;
	double width, height;

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (context != NULL);
	g_return_if_fail (cr != NULL);
	g_return_if_fail (range != NULL);

	cairo_save (cr);

	height = sheet_row_get_distance_pts (sheet, range->start.row,
					    range->end.row + 1);
	width = sheet_col_get_distance_pts (sheet,
					     range->start.col, range->end.col + 1);

	if (sheet->text_is_rtl)
		cairo_rectangle (cr,
				 base_x - width, base_y,
				 width, height);
	else
		cairo_rectangle (cr,
				 base_x, base_y,
				 width, height);
	cairo_clip (cr);

	objects = g_slist_reverse (g_slist_copy (sheet->sheet_objects));

	for (ptr = objects; ptr; ptr = ptr->next) {
		SheetObject *so = SHEET_OBJECT (ptr->data);
		GnmRange const *r = &so->anchor.cell_bound;

		if (!sheet_object_can_print (so) ||
		    !range_overlap (range, &so->anchor.cell_bound))
			continue;

		cairo_save (cr);
		/* move to top left */
		if (sheet->text_is_rtl) {
			double tr_x, tr_y;
			tr_x =  base_x
				- sheet_col_get_distance_pts (sheet, 0, r->end.col+1)
				+ sheet_col_get_distance_pts (sheet, 0,
							      range->start.col);
			tr_y = - base_y
				+ sheet_row_get_distance_pts (sheet, 0, r->start.row)
				- sheet_row_get_distance_pts (sheet, 0,
							      range->start.row);
			cairo_translate (cr, tr_x, tr_y);
		} else
			cairo_translate (cr,
					 - base_x
					 + sheet_col_get_distance_pts (sheet, 0, r->start.col)
					 - sheet_col_get_distance_pts (sheet, 0,
								       range->start.col),
					 - base_y
					 + sheet_row_get_distance_pts (sheet, 0, r->start.row)
					 - sheet_row_get_distance_pts (sheet, 0,
								       range->start.row));

		sheet_object_draw_cairo (so, (gpointer)cr, sheet->text_is_rtl);
		cairo_restore (cr);
	}

	g_slist_free (objects);

	cairo_restore (cr);
}

static void
print_page_cells (GtkPrintContext   *context, PrintingInstance * pi,
		  cairo_t *cr, Sheet const *sheet, GnmRange *range,
		  double base_x, double base_y)
{	PrintInformation const *pinfo = sheet->print_info;

	gnm_gtk_print_cell_range (context, cr, sheet, range,
				  base_x, base_y, !pinfo->print_grid_lines);
	print_sheet_objects (context, cr, sheet, range, base_x, base_y);
}

static void
print_header_gtk (GtkPrintContext   *context, cairo_t *cr,
		  float x, float y, float w, float h,
		  char const *name,
		  PangoFontDescription *desc)
{
	PangoLayout *layout;
	gint layout_height;
	gdouble text_height;

	cairo_rectangle (cr, x, y, w, h);
	cairo_set_source_rgb (cr, 0.8, 0.8, 0.8);
	cairo_fill (cr);

	cairo_set_source_rgb (cr, 0., 0., 0.);
	layout = gtk_print_context_create_pango_layout (context);
	pango_layout_set_font_description (layout, desc);

	pango_layout_set_text (layout, name, -1);
	pango_layout_set_width (layout, w);
	pango_layout_set_alignment (layout, PANGO_ALIGN_CENTER);

	pango_layout_get_size (layout, NULL, &layout_height);
	text_height = (gdouble)layout_height / PANGO_SCALE;

	cairo_move_to (cr, x + w/2,  y + (h - text_height) / 2);
	pango_cairo_show_layout (cr, layout);

	g_object_unref (layout);

}

static void
print_page_col_headers (GtkPrintContext   *context, PrintingInstance * pi,
		  cairo_t *cr, Sheet const *sheet, GnmRange *range,
		  double row_header_width, double col_header_height)
{
	int start_col, end_col;
	int col;
	double x;
	PangoFontDescription *desc;

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (range != NULL);
	g_return_if_fail (range->start.col <= range->end.col);

	desc = pango_font_description_from_string ("sans 12");

	start_col = range->start.col;
	end_col = range->end.col;

	x = (row_header_width + GNM_COL_MARGIN) * (sheet->text_is_rtl ? -1. : 1.);

	for (col = start_col; col <= end_col ; col++) {
		ColRowInfo const *ci = sheet_col_get_info (sheet, col);

		if (ci->visible) {
			if (sheet->text_is_rtl)
				x -= ci->size_pts;

			print_header_gtk (context, cr,
					  x + 0.5, 0,
					  ci->size_pts - 1,
					  col_header_height - 0.5,
					  col_name (col), desc);

			if (!sheet->text_is_rtl)
				x += ci->size_pts;
		}
	}

	pango_font_description_free (desc);
}

static void
print_page_row_headers (GtkPrintContext   *context, PrintingInstance * pi,
		  cairo_t *cr, Sheet const *sheet, GnmRange *range,
		  double row_header_width, double col_header_height)
{
	int start_row, end_row;
	int row;
	double x = 0, y;
	PangoFontDescription *desc;

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (range != NULL);
	g_return_if_fail (range->start.row <= range->end.row);

	desc = pango_font_description_from_string ("sans 12");

	start_row = range->start.row;
	end_row = range->end.row;

	if (sheet->text_is_rtl)
		x = - (row_header_width - 0.5);

	for (row = start_row, y = col_header_height; row <= end_row ; row++) {
		ColRowInfo const *ri = sheet_row_get_info (sheet, row);

		if (ri->visible) {
			print_header_gtk (context, cr,
					  x, y + 0.5,
					  row_header_width - 0.5,
					  ri->size_pts - 1,
					  row_name (row), desc);
			y += ri->size_pts;
		}
	}

	pango_font_description_free (desc);
}

static PangoLayout *
ensure_decoration_layout (GtkPrintContext   *context)
{
	GnmStyle *style;
	GnmFont *font;
	PangoLayout *layout;

	layout = gtk_print_context_create_pango_layout (context);
	style = gnm_style_dup (gnm_app_prefs->printer_decoration_font);
	font = gnm_style_get_font
		(style,
		 pango_layout_get_context (layout),
		 1.);
	pango_layout_set_font_description (layout, font->go.font->desc);
	gnm_style_unref (style);

	return layout;
}


/*
 * print_hf_element
 * @pj: printing context
 * @format:
 * @side:
 * @y:
 *
 * Print a header/footer line.
 *
 * Position at y, and clip to rectangle. If gnm_print_debug is TRUE, display
 * the rectangle.
 */
static void
print_hf_element (GtkPrintContext   *context, cairo_t *cr, Sheet const *sheet, 
		  char const *format,
		  PangoAlignment side, gdouble width, gboolean align_bottom, 
		  HFRenderInfo *hfi)
{
	PangoLayout *layout;
	
	gdouble text_height = 0.;
	char *text;

	if (format == NULL)
		return;

	text = hf_format_render (format, hfi, HF_RENDER_PRINT);
	
	if (text == NULL)
		return;

	layout = ensure_decoration_layout (context);
	
	pango_layout_set_text (layout, text, -1);
	pango_layout_set_width (layout, width * PANGO_SCALE);
	pango_layout_set_alignment (layout, side);
	
	if (align_bottom) {
		gint layout_height = 0;
		pango_layout_get_size (layout, NULL, &layout_height);
		text_height = (gdouble)layout_height / PANGO_SCALE;
	}
		
	cairo_move_to (cr, 0., - text_height);
	pango_cairo_show_layout (cr, layout);
		
	g_object_unref (layout);
	g_free(text);
}

/*
 * print_hf_line
 * @hf:     header/footer descriptor
 * @align_bottom:      vertical position (whether to print above or below
 * @width:  width of header line
 *
 * Print a header/footer line.
 *
 */
static void
print_hf_line (GtkPrintContext   *context, cairo_t *cr, Sheet const *sheet, 
	       PrintHF const *hf, gboolean align_bottom, gdouble width, HFRenderInfo *hfi)
{
	print_hf_element (context, cr, sheet, hf->left_format, PANGO_ALIGN_LEFT, width, align_bottom, hfi);
	print_hf_element (context, cr, sheet, hf->middle_format, PANGO_ALIGN_CENTER, width, align_bottom, hfi);
	print_hf_element (context, cr, sheet, hf->right_format, PANGO_ALIGN_RIGHT, width, align_bottom, hfi);
}



/**
 * print_page:
 * @pj:        printing context
 * @gsr :      the page information
 *
 * Excel prints repeated rows like this: Pages up to and including the page
 * where the first of the repeated rows "naturally" occurs are printed in
 * the normal way. On subsequent pages, repated rows are printed before the
 * regular flow.
 */
static gboolean
print_page (GtkPrintOperation *operation,
	    GtkPrintContext   *context,
	    PrintingInstance * pi,
	    SheetPageRange *gsr)
{
	Sheet *sheet = gsr->sheet;
	PrintInformation *pinfo = sheet->print_info;
	gdouble print_height, print_width;
	gdouble main_height, main_width;
	gdouble header, footer, left, right;
	gdouble edge_to_below_header, edge_to_above_footer;
	cairo_t *cr;
	gdouble px, py;
	gdouble width;
	gdouble height;
	gdouble col_header_height = 0.;
	gdouble row_header_width = 0.;
	gdouble rep_row_height = 0.;
	gdouble rep_col_width = 0.;
	gdouble dir = (sheet->text_is_rtl ? -1. : 1.);
	GnmRange r_repeating_intersect;

	px = pinfo->scaling.percentage.x / 100.;
	py = pinfo->scaling.percentage.y / 100.;

	if (px <= 0.)
		px = 1.;
	if (py <= 0.)
		py = 1.;

	cr = gtk_print_context_get_cairo_context (context);
	print_info_get_margins (pinfo, &header, &footer, &left, &right,
				&edge_to_below_header, &edge_to_above_footer);

	if (sheet->print_info->print_titles) {
		col_header_height = sheet->rows.default_style.size_pts;
		row_header_width = sheet->cols.default_style.size_pts;
	}

	width = gtk_print_context_get_width (context);
	height = print_info_get_paper_height (pinfo,GTK_UNIT_POINTS)
		- edge_to_below_header - edge_to_above_footer;

	main_height = sheet_row_get_distance_pts (sheet, gsr->range.start.row,
						   gsr->range.end.row + 1);
	main_width = sheet_col_get_distance_pts (sheet, gsr->range.start.col,
						  gsr->range.end.col + 1);
	if (gsr->n_rep_rows > 0)
		rep_row_height = sheet_row_get_distance_pts 
			(sheet, gsr->first_rep_rows,
			 gsr->first_rep_rows + gsr->n_rep_rows);
	if (gsr->n_rep_cols > 0)
		rep_col_width = sheet_col_get_distance_pts 
			(sheet, gsr->first_rep_cols,
			 gsr->first_rep_cols + gsr->n_rep_cols);
        if ((gsr->n_rep_rows > 0) || (gsr->n_rep_cols > 0)) {
                range_init (&r_repeating_intersect, gsr->first_rep_cols, gsr->first_rep_rows,
				gsr->first_rep_cols + gsr->n_rep_cols - 1,
			        gsr->first_rep_rows + gsr->n_rep_rows - 1);
       }

	print_height = main_height + col_header_height + rep_row_height;
	print_width = main_width + row_header_width + rep_col_width;

	/* printing header  */
	
	if (edge_to_below_header > header) {
		cairo_save (cr);
		print_hf_line (context, cr, sheet, pinfo->header, 
			       FALSE, width, pi->hfi);
		cairo_restore (cr);
	}
	
	/* printing footer  */
	
	if (edge_to_above_footer > footer) {
		cairo_save (cr);
		cairo_translate (cr, 0, height + header + footer);
		print_hf_line (context, cr, sheet, pinfo->footer, TRUE, width, 
			       pi->hfi);
		cairo_restore (cr);
	}

/* setting up content area */
	cairo_save (cr);
	cairo_translate (cr, sheet->text_is_rtl ? width : 0, edge_to_below_header - header);
	if (pinfo->center_horizontally == 1 || pinfo->center_vertically == 1) {
		double shift_x = 0;
		double shift_y = 0;

		if (pinfo->center_horizontally == 1)
			shift_x = (width - print_width * px)/2;
		if (pinfo->center_vertically == 1)
			shift_y = (height - print_height * py)/2;
		cairo_translate (cr, dir * shift_x, shift_y);
	}
	cairo_scale (cr, px, py);

/* printing column and row headers */

	if (sheet->print_info->print_titles) {
		cairo_save (cr);
		if (gsr->n_rep_cols > 0) {
			print_page_col_headers (context, pi, cr, sheet, 
						&r_repeating_intersect, 
						row_header_width, col_header_height);
			cairo_translate (cr, dir * rep_col_width, 0 );
		}
		print_page_col_headers (context, pi, cr, sheet, &gsr->range, 
					row_header_width, col_header_height);
		cairo_restore (cr);
		cairo_save (cr);
		if (gsr->n_rep_rows > 0) {
			print_page_row_headers (context, pi, cr, sheet, 
						&r_repeating_intersect, 
						row_header_width, col_header_height);
			cairo_translate (cr, 0, rep_row_height);
		}
		print_page_row_headers (context, pi, cr, sheet, &gsr->range, 
					row_header_width, col_header_height);
		cairo_restore (cr);
		cairo_translate (cr, dir * row_header_width, col_header_height);
	}

/* printing repeated row/col intersect */

	if ((gsr->n_rep_rows > 0) && (gsr->n_rep_cols > 0)) {
		print_page_cells (context, pi, cr, sheet, 
				  &r_repeating_intersect, 
				  dir * GNM_COL_MARGIN, -GNM_ROW_MARGIN);
	}

/* printing repeated rows  */

	if (gsr->n_rep_rows > 0) {
		GnmRange r;
		range_init (&r, gsr->range.start.col, gsr->first_rep_rows,
			    gsr->range.end.col, gsr->first_rep_rows + gsr->n_rep_rows - 1);
		cairo_save (cr);
		if (gsr->n_rep_cols > 0)
			cairo_translate (cr, dir * rep_col_width, 0 );
		print_page_cells (context, pi, cr, sheet, &r, 
				  dir * GNM_COL_MARGIN, -GNM_ROW_MARGIN);
		cairo_restore (cr);
		cairo_translate (cr, 0, rep_row_height );
	}

/* printing repeated cols */

	if (gsr->n_rep_cols > 0) {
		GnmRange r;
		range_init (&r, gsr->first_rep_cols, gsr->range.start.row,
			    gsr->first_rep_cols + gsr->n_rep_cols - 1, gsr->range.end.row);
		print_page_cells (context, pi, cr, sheet, &r, 
				  dir * GNM_COL_MARGIN, -GNM_ROW_MARGIN);
		cairo_translate (cr, dir * rep_col_width, 0 );
	}

/* printing page content  */

	print_page_cells (context, pi, cr, sheet, &gsr->range, 
			  dir * GNM_COL_MARGIN, -GNM_ROW_MARGIN);

	cairo_restore (cr);
	return 1;
}

/*
 * Computes number of rows or columns that fit in @usable starting
 * at index @start and limited to index @end
 */
static int
compute_group (Sheet const *sheet,
	       int start, int end, double usable,
	       ColRowInfo const *(get_info)(Sheet const *sheet, int const p))
{
	double size_pts = 1.; /* The initial grid line */
	int idx, count = 0;

	for (idx = start; idx <= end; idx++, count++) {
		ColRowInfo const *info = (*get_info) (sheet, idx);
		if (info->visible) {
			size_pts += info->size_pts;
			if (size_pts > usable)
				break;
		}
	}

	/* FIXME : Find a way to inform the user that one of the rows/cols does
	 * not fit on a page
	 */
	g_return_val_if_fail (count > 0, 1);

	return count;
}

static void
adjust_repetition (Sheet const *sheet,
		   gint i,
		   gint first_rep,
		   gint n_rep,
		   gdouble repeating,
		   gint *first_rep_used,
		   gint *n_rep_used,
		   gdouble *repeating_used,
		   double (sheet_get_distance_pts) (Sheet const *sheet, int from, int to))
{
	if (i > first_rep) {
		*first_rep_used = first_rep;
		if (i - first_rep < n_rep) {
			*n_rep_used = i - first_rep;
			*repeating_used = sheet_get_distance_pts 
				(sheet, first_rep, first_rep + *n_rep_used);
		} else {
			*repeating_used = repeating;
			*n_rep_used = n_rep;
		}
	}
}

static gint 
paginate (GSList **paginationInfo,
	  Sheet const *sheet,
	  gint start, gint end,
	  gdouble usable, gboolean repeat, gint repeat_start, gint repeat_end,
	  double (sheet_get_distance_pts) (Sheet const *sheet, int from, int to),
	  ColRowInfo const *(get_info)(Sheet const *sheet, int const p))
{
	GSList *list = NULL;
	int rc = start;
	gint n_rep = 0, first_rep = 0;
	gdouble repeating = 0.;
	gint page_count = 0;

	if (repeat) {
		first_rep = repeat_start;
		n_rep = repeat_end - first_rep + 1;
		repeating = sheet_get_distance_pts (sheet, first_rep, first_rep + n_rep);
	} 

	while (rc <= end) {
		int count;
		PaginationInfo *item;
 
		gdouble repeating_used = 0.;
		gint n_rep_used = 0, first_rep_used = 0;

		adjust_repetition (sheet, rc,
				   first_rep, n_rep,
				   repeating,
				   &first_rep_used, &n_rep_used,
				   &repeating_used,
				   sheet_get_distance_pts);

		count = compute_group (sheet, rc, end,
				       usable - repeating_used, 
				       get_info);
	
		if (paginationInfo) {
			item = g_new (PaginationInfo,1);
			item->rc = rc;
			item->count = count;
			item->first_rep = first_rep_used;
			item->n_rep = n_rep_used;

			list = g_slist_prepend (list, item);
		}
		page_count++;

		rc += count;
	}

	if (paginationInfo) {
		list = g_slist_reverse (list);

		*paginationInfo = list;
	}

	return page_count;
}

/* computer_scale_fit_to
 * Computes the scaling needed to fit all the rows or columns into the @usable
 * area.
 * This function is called when printing, and the user has selected the 'fit-to'
 * printing option. It will adjust the internal x and y scaling values to
 * make the sheet fit the desired number of pages, as suggested by the user.
 * It will only reduce the scaling to fit inside a page, not enlarge.
 */
static double
compute_scale_fit_to (Sheet const *sheet,
		      int start, int end, double usable,
		      ColRowInfo const *(get_info)(Sheet const *sheet, int const p),
		      double (get_distance_pts) (Sheet const *sheet, int from, int to),
		      gint pages, double max_percent, double header,
		      gboolean repeat, gint repeat_start, gint repeat_end)
{
	double max_p, min_p;
	gint   max_pages, min_pages;
	double extent;

	extent = get_distance_pts (sheet, start, end + 1);

	/* If the repeating columns are not included we should add them */
	if (repeat && (repeat_start < start))
		extent += get_distance_pts (sheet, repeat_start, 
					    (repeat_end < start) ? (repeat_end + 1) : start);

	/* This means to take whatever space is needed.  */
	if (pages <= 0)
		return max_percent;

	/* We can handle a single page easily: */
	if (pages == 1) {
		max_p = usable/(header + extent + 2.);
		return ((max_p > max_percent) ? max_percent : max_p);
	}

	/* There is no easy way to calculate really which percentage is needed */
	/* without in fact allocating the cols/rows to pages.                  */

	/* We first calculate the max percentage needed */

	max_p = (pages * usable)/(extent + pages * header);
	if (max_p > max_percent)
		max_p = max_percent;

	max_pages = paginate (NULL, sheet, start, end, usable/max_p - header,
			      repeat, repeat_start, repeat_end,
			      get_distance_pts, get_info);

	if (max_pages == pages)
		return max_p;

	/* The we calculate the min percentage */

	min_p = usable/(extent + header);
	if (min_p > max_percent)
		min_p = max_percent;

	min_pages = paginate (NULL, sheet, start, end, usable/min_p - header,
			      repeat, repeat_start, repeat_end,
			      get_distance_pts, get_info);


	/* And then we pick the middle until the percentage is within 0.1% of */
	/* the desired percentage */

	while (max_p - min_p > 0.001) {
		double cur_p = (max_p + min_p) / 2.;
		int cur_pages = paginate (NULL, sheet, start, end, usable/cur_p - header,
					  repeat, repeat_start, repeat_end,
					  get_distance_pts, get_info);

		if (cur_pages > pages) {
			max_pages = cur_pages;
			max_p = cur_p;
		} else {
			min_pages = cur_pages;
			min_p = cur_p;
		}
	}

	return min_p;
}

#define COL_FIT(col) (col >= gnm_sheet_get_max_cols (sheet) ? (gnm_sheet_get_max_cols (sheet)-1) : col)
#define ROW_FIT(row) (row >= gnm_sheet_get_max_rows (sheet) ? (gnm_sheet_get_max_rows (sheet)-1) : row)

static void
compute_sheet_pages_add_sheet (PrintingInstance * pi, Sheet const *sheet, gboolean selection,
                     gboolean ignore_printarea)
{
	SheetPrintInfo *spi = g_new (SheetPrintInfo, 1);

	spi->sheet = (Sheet *) sheet;
	spi->selection = selection;
	spi->ignore_printarea = ignore_printarea;
	pi->gnmSheets = g_list_append(pi->gnmSheets, spi);
}

static void
compute_sheet_pages_add_range (PrintingInstance * pi, Sheet const *sheet,
			       GnmRange *r, gint n_rep_cols, gint n_rep_rows,
			       gint first_rep_cols, gint first_rep_rows)
{
		SheetPageRange *gsr = g_new (SheetPageRange,1);

		gsr->n_rep_cols = n_rep_cols;
		gsr->n_rep_rows = n_rep_rows;
		gsr->first_rep_rows = first_rep_rows;
		gsr->first_rep_cols = first_rep_cols;
		gsr->range = *r;
		gsr->sheet = (Sheet *) sheet;
		pi->gnmSheetRanges = g_list_append(pi->gnmSheetRanges, gsr);
}



static void
compute_sheet_pages_down_then_across (PrintingInstance * pi,
				      Sheet const *sheet,
				      GSList *column_pagination,
				      GSList *row_pagination)
{
	GnmRange range;
	GSList *c_list = column_pagination;

	while (c_list) {
		PaginationInfo *c_info = c_list->data;
		GSList *r_list = row_pagination;

		while (r_list) {
			PaginationInfo *r_info = r_list->data;
			
			range_init (&range, COL_FIT (c_info->rc), ROW_FIT (r_info->rc),
				    COL_FIT (c_info->rc + c_info->count - 1),
				    ROW_FIT (r_info->rc + r_info->count - 1));
			compute_sheet_pages_add_range (pi, sheet, &range, 
						       c_info->n_rep, r_info->n_rep,
						       c_info->first_rep, r_info->first_rep);
			r_list = r_list->next;
		}
		c_list = c_list->next;
	}
}

static void
compute_sheet_pages_across_then_down (PrintingInstance * pi,
				      Sheet const *sheet,
				      GSList *column_pagination,
				      GSList *row_pagination)
{
	GnmRange range;
	GSList *r_list = row_pagination;

	while (r_list) {
		PaginationInfo *r_info = r_list->data;
		GSList *c_list = column_pagination;

		while (c_list) {
			PaginationInfo *c_info = c_list->data;
			
			range_init (&range, COL_FIT (c_info->rc), ROW_FIT (r_info->rc),
				    COL_FIT (c_info->rc + c_info->count - 1),
				    ROW_FIT (r_info->rc + r_info->count - 1));
			compute_sheet_pages_add_range (pi, sheet, &range, 
						       c_info->n_rep, r_info->n_rep,
						       c_info->first_rep, r_info->first_rep);
			c_list = c_list->next;
		}
		r_list = r_list->next;
	}
}


/*
  return TRUE in case of trouble
*/

static gboolean
compute_sheet_pages (GtkPrintContext   *context,
		     PrintingInstance * pi,
		     SheetPrintInfo *spi)
{
	Sheet *sheet = spi->sheet;
	PrintInformation *pinfo = sheet->print_info;
	GnmRange r;
	GnmRange const *selection_range;
	GnmRange print_area;
	gdouble col_header_height = 0.;
	gdouble row_header_width = 0.;
	gdouble page_width, page_height;
	gdouble top_margin, bottom_margin, edge_to_below_header, edge_to_above_footer;
	gdouble px, py;
	gdouble usable_x, usable_y;

	GSList *column_pagination = NULL;
	GSList *row_pagination = NULL;

	if (pinfo->print_titles) {
		col_header_height = sheet->rows.default_style.size_pts;
		row_header_width = sheet->cols.default_style.size_pts;
	}

	print_area = sheet_get_printarea (sheet,
					  pinfo->print_even_if_only_styles,
					  spi->ignore_printarea);
	if (spi->selection) {
		selection_range = selection_first_range
			(sheet_get_view (sheet, wb_control_view (pi->wbc)),
			  GO_CMD_CONTEXT (pi->wbc), _("Print Selection"));
		if (selection_range == NULL)
			return TRUE;
		if (spi->ignore_printarea) {
			r = *selection_range;
		} else {
			if (!range_intersection (&r, selection_range, &print_area))
				return FALSE;
		}
	} else
		r = print_area;

	page_width = gtk_print_context_get_width (context);
	page_height = gtk_print_context_get_height (context);
	print_info_get_margins (pinfo, &top_margin, &bottom_margin, NULL, NULL,
				&edge_to_below_header, &edge_to_above_footer);
	page_height -= ((edge_to_below_header - top_margin)
			+ (edge_to_above_footer - bottom_margin));

	if (pinfo->scaling.type == PRINT_SCALE_FIT_PAGES) {
		/* Note that the resulting scale is independent from */
		/* whether we print first down or across!            */
		gdouble pxy;

		pxy = compute_scale_fit_to (sheet, r.start.row, r.end.row,
					    page_height, sheet_row_get_info,
					    sheet_row_get_distance_pts,
					    pinfo->scaling.dim.rows, 1.,
					    col_header_height,
					    pinfo->repeat_top.use, pinfo->repeat_top.range.start.row, 
					    pinfo->repeat_top.range.end.row);
		pxy = compute_scale_fit_to (sheet, r.start.col, r.end.col,
					    page_width, sheet_col_get_info,
					    sheet_col_get_distance_pts,
					    pinfo->scaling.dim.cols, pxy,
					    row_header_width,
					    pinfo->repeat_left.use, pinfo->repeat_left.range.start.col, 
					    pinfo->repeat_left.range.end.col);

		pinfo->scaling.percentage.x = pxy * 100.;
		pinfo->scaling.percentage.y = pxy * 100.;
	}

	px = pinfo->scaling.percentage.x / 100.;
	py = pinfo->scaling.percentage.y / 100.;

	if (px <= 0.)
		px = 1.;
	if (py <= 0.)
		py = 1.;

	usable_x   = page_width / px;
	usable_y   = page_height / py;

	paginate (&column_pagination, sheet, r.start.col, r.end.col, 
		  usable_x - row_header_width,
		  pinfo->repeat_left.use, pinfo->repeat_left.range.start.col, 
		  pinfo->repeat_left.range.end.col, 
		  sheet_col_get_distance_pts, sheet_col_get_info);
	paginate (&row_pagination, sheet, r.start.row, r.end.row, 
		  usable_y - col_header_height,
		  pinfo->repeat_top.use, pinfo->repeat_top.range.start.row, 
		  pinfo->repeat_top.range.end.row,
		  sheet_row_get_distance_pts, sheet_row_get_info);

	if (sheet->print_info->print_across_then_down)
		compute_sheet_pages_across_then_down (pi, sheet, 
						      column_pagination,row_pagination);
	else
		compute_sheet_pages_down_then_across (pi, sheet,
						      column_pagination,row_pagination);

	go_slist_free_custom (column_pagination, g_free);
	go_slist_free_custom (row_pagination, g_free);
	
	return FALSE;
}

/*
 * Computes the pages that will be output by a specific
 * print request.
 */
static void
compute_pages (GtkPrintOperation *operation,
	       PrintingInstance * pi,
	       PrintRange pr,
	       guint from,
	       guint to)
{
	Workbook *wb = pi->wb;
	guint i;
	guint n;

	switch (pr) {
	case PRINT_ACTIVE_SHEET:
		compute_sheet_pages_add_sheet (pi, pi->sheet, FALSE, FALSE);
		break;
	case PRINT_ALL_SHEETS:
		n = workbook_sheet_count (wb);
		for (i = 0; i < n; i++) {
			Sheet *sheet = workbook_sheet_by_index (wb, i);
			if (sheet->print_info->do_not_print)
				continue;
			compute_sheet_pages_add_sheet (pi, sheet,
						       FALSE, FALSE);
		}
		break;
	case PRINT_SHEET_RANGE:
		n = workbook_sheet_count (wb);
		if (to > n)
			to = n;
		for (i = from - 1; i < to; i++){
			Sheet *sheet = workbook_sheet_by_index (wb, i);
			if (sheet->print_info->do_not_print)
				continue;
			compute_sheet_pages_add_sheet (pi, sheet,
						       FALSE, FALSE);
		}
		break;
	case PRINT_SHEET_SELECTION:
		compute_sheet_pages_add_sheet (pi, pi->sheet, TRUE, FALSE);
		break;
	case PRINT_IGNORE_PRINTAREA:
		compute_sheet_pages_add_sheet (pi, pi->sheet, FALSE, TRUE);
		break;
	case PRINT_SHEET_SELECTION_IGNORE_PRINTAREA:
		compute_sheet_pages_add_sheet (pi, pi->sheet, TRUE, TRUE);
		break;
	}
	return;
}

#if 0

static PrintJobInfo *
print_job_info_get (Sheet *sheet, PrintRange range, gboolean const preview)
{
	PrintJobInfo *pj = g_new0 (PrintJobInfo, 1);

	pj->gp_config = print_info_make_config (sheet->print_info);

	/* Values that should be entered in a dialog box */
	pj->start_page = 0;
	pj->end_page = workbook_sheet_count (sheet->workbook) - 1;
	pj->range = range;
	pj->sorted_print = TRUE;
	pj->is_preview = preview;
	pj->current_output_sheet = 0;

	/*
	 * Setup render info
	 */
	pj->render_info = hf_render_info_new ();
	pj->render_info->sheet = sheet;
	pj->render_info->page = 1;

	return pj;
}

static void
print_job_info_destroy (PrintJobInfo *pj)
{
	g_object_unref (pj->gp_config);
	hf_render_info_destroy (pj->render_info);
	if (pj->decoration_layout)
		g_object_unref (pj->decoration_layout);
	if (pj->print_context)
		g_object_unref (pj->print_context);
	g_free (pj);
}

#endif

static gboolean
gnm_paginate_cb (GtkPrintOperation *operation,
		 GtkPrintContext   *context,
		 gpointer           user_data)
{
	PrintingInstance * pi = (PrintingInstance *) user_data;
	guint paginate = (pi->last_pagination)++;
	SheetPrintInfo *spi;
	gint n_pages;

	spi = g_list_nth_data (pi->gnmSheets, paginate);
	if (spi == NULL) { /*We are done paginating */
		n_pages = g_list_length (pi->gnmSheetRanges);

		gtk_print_operation_set_n_pages (operation, n_pages == 0 ? 1 : n_pages);
		gtk_print_operation_set_unit (operation, GTK_UNIT_POINTS);
		pi->hfi->pages = n_pages;

		if (n_pages == 0) /* gtk+ cannot handle 0 pages */
			gtk_print_operation_cancel (operation);

		return TRUE;
	}

	if (compute_sheet_pages (context, pi, spi)) {
		gtk_print_operation_cancel (operation);
		return TRUE; 
	}

	return FALSE;
}

static void
gnm_begin_print_cb (GtkPrintOperation *operation,
                    GtkPrintContext   *context,
		    gpointer           user_data)
{
	PrintingInstance * pi = (PrintingInstance *) user_data;
	PrintRange pr;
	guint from, to;
	GtkPrintSettings * settings;

	settings =  gtk_print_operation_get_print_settings (operation);

	from = gtk_print_settings_get_int_with_default
		(settings, GNUMERIC_PRINT_SETTING_PRINT_FROM_SHEET_KEY, 1);
	to = gtk_print_settings_get_int_with_default
		(settings, GNUMERIC_PRINT_SETTING_PRINT_TO_SHEET_KEY, workbook_sheet_count (pi->wb));
	pr = gtk_print_settings_get_int_with_default
		(settings, GNUMERIC_PRINT_SETTING_PRINTRANGE_KEY, PRINT_ACTIVE_SHEET);
	if (from != pi->from || to != pi->to || pr != pi->pr) {
		/* g_warning ("Working around gtk+ bug 423484."); */
		gtk_print_settings_set_int
			(settings, GNUMERIC_PRINT_SETTING_PRINT_FROM_SHEET_KEY,
			 pi->from);
		gtk_print_settings_set_int
			(settings, GNUMERIC_PRINT_SETTING_PRINT_TO_SHEET_KEY,
			 pi->to);
		gtk_print_settings_set_int
			(settings, GNUMERIC_PRINT_SETTING_PRINTRANGE_KEY, pi->pr);
		from = pi->from;
		to = pi->to;
		pr = pi->pr;
	}

	compute_pages (operation, pi, pr, from, to);
}

static void
gnm_end_print_cb (GtkPrintOperation *operation,
                  GtkPrintContext   *context,
                  gpointer           user_data)
{
	PrintingInstance * pi = (PrintingInstance *) user_data;
	printing_instance_delete (pi);
}

static void
cp_gtk_page_setup (GtkPageSetup *from, GtkPageSetup *to)
{
	gtk_page_setup_set_paper_size (to, gtk_page_setup_get_paper_size (from));
	gtk_page_setup_set_orientation (to,gtk_page_setup_get_orientation  (from));
	gtk_page_setup_set_top_margin
		(to, gtk_page_setup_get_top_margin (from, GTK_UNIT_MM), GTK_UNIT_MM);
	gtk_page_setup_set_bottom_margin
		(to, gtk_page_setup_get_bottom_margin (from, GTK_UNIT_MM), GTK_UNIT_MM);
	gtk_page_setup_set_left_margin
		(to, gtk_page_setup_get_left_margin (from, GTK_UNIT_MM), GTK_UNIT_MM);
	gtk_page_setup_set_right_margin
		(to, gtk_page_setup_get_right_margin (from, GTK_UNIT_MM), GTK_UNIT_MM);
}

static void
gnm_request_page_setup_cb (GtkPrintOperation *operation,
                           GtkPrintContext   *context,
			   gint               page_nr,
			   GtkPageSetup      *setup,
			   gpointer           user_data)
{
	PrintingInstance * pi = (PrintingInstance *) user_data;
	SheetPageRange * gsr;
	GtkPrintSettings* settings = gtk_print_operation_get_print_settings
				     (operation);

	g_return_if_fail (pi != NULL);

	gsr = g_list_nth_data (pi->gnmSheetRanges, page_nr);
	if (gsr == NULL) {
		/* g_warning ("Avoiding gtk+ bug 492498"); */
		return;
	}

	gtk_print_settings_set_use_color (settings, !gsr->sheet->print_info->print_black_and_white);
	if (gsr->sheet->print_info->page_setup == NULL)
		print_info_load_defaults (gsr->sheet->print_info);
	if (gsr->sheet->print_info->page_setup != NULL)
		cp_gtk_page_setup (gsr->sheet->print_info->page_setup, setup);
}

static void
gnm_draw_page_cb (GtkPrintOperation *operation,
                  GtkPrintContext   *context,
		  gint               page_nr,
		  gpointer           user_data)
{

	PrintingInstance * pi = (PrintingInstance *) user_data;
	SheetPageRange * gsr = g_list_nth_data (pi->gnmSheetRanges,
					       page_nr);
	if (gsr) {
		pi->hfi->page = page_nr + 1;
		pi->hfi->sheet = gsr->sheet;
		pi->hfi->page_area = gsr->range;
		pi->hfi->top_repeating = gsr->range.start;
		if (gsr->n_rep_cols > 0)
			pi->hfi->top_repeating.col = gsr->first_rep_cols;
		if (gsr->n_rep_rows > 0)
			pi->hfi->top_repeating.row = gsr->first_rep_rows;
		print_page (operation, context, pi, gsr);
	}
}

static void
widget_button_cb (GtkToggleButton *togglebutton, GtkWidget *check)
{
	gtk_widget_set_sensitive (check, gtk_toggle_button_get_active (togglebutton));
}

static GObject*
gnm_create_widget_cb (GtkPrintOperation *operation, gpointer user_data)
{
	PrintingInstance * pi = (PrintingInstance *) user_data;
	GtkWidget *frame, *table;
	GtkWidget *button_all_sheets, *button_selected_sheet, *button_spec_sheets;
	GtkWidget *button_selection, *button_ignore_printarea;
	GtkWidget *label_from, *label_to;
	GtkWidget *spin_from, *spin_to;
	GtkPrintSettings * settings;
	guint n_sheets = workbook_sheet_count (pi->wb);

	frame = gtk_frame_new (NULL);
	gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_NONE);
	gtk_container_set_border_width (GTK_CONTAINER (frame), 5);

	table = gtk_table_new (7, 6, FALSE);
	gtk_table_set_col_spacing (GTK_TABLE (table), 1,20);
	gtk_container_add (GTK_CONTAINER (frame), table);


	button_all_sheets = gtk_radio_button_new_with_mnemonic (NULL,
								_("_All workbook sheets"));
	gtk_table_attach (GTK_TABLE (table), button_all_sheets, 1, 3, 1, 2,
			  GTK_EXPAND | GTK_FILL,GTK_SHRINK | GTK_FILL,0,0);

	button_selected_sheet = gtk_radio_button_new_with_mnemonic_from_widget
		(GTK_RADIO_BUTTON (button_all_sheets), _("A_ctive workbook sheet"));
	gtk_table_attach (GTK_TABLE (table), button_selected_sheet, 1, 3, 2, 3,
			  GTK_EXPAND | GTK_FILL,GTK_SHRINK | GTK_FILL,0,0);

	button_spec_sheets = gtk_radio_button_new_with_mnemonic_from_widget
		(GTK_RADIO_BUTTON (button_all_sheets), _("_Workbook sheets:"));
	gtk_table_attach (GTK_TABLE (table), button_spec_sheets, 1, 3, 5, 6,
			  GTK_EXPAND | GTK_FILL,GTK_SHRINK | GTK_FILL,0,0);

	button_selection = gtk_check_button_new_with_mnemonic
		(_("Current _selection only"));
	gtk_table_attach (GTK_TABLE (table), button_selection, 2, 7, 3, 4,
			  GTK_EXPAND | GTK_FILL,GTK_SHRINK | GTK_FILL,0,0);

	button_ignore_printarea  = gtk_check_button_new_with_mnemonic
		(_("_Ignore defined print area"));
	gtk_table_attach (GTK_TABLE (table), button_ignore_printarea, 2, 7, 4, 5,
			  GTK_EXPAND | GTK_FILL,GTK_SHRINK | GTK_FILL,0,0);

	label_from = gtk_label_new (_("from:"));
	gtk_table_attach (GTK_TABLE (table), label_from, 3, 4, 5, 6,
			  GTK_EXPAND | GTK_FILL,GTK_SHRINK | GTK_FILL,0,0);

	spin_from = gtk_spin_button_new_with_range (1, n_sheets, 1);
	gtk_table_attach (GTK_TABLE (table), spin_from, 4, 5, 5, 6,
			  GTK_EXPAND | GTK_FILL,GTK_SHRINK | GTK_FILL,0,0);

	label_to = gtk_label_new (_("to:"));
	gtk_table_attach (GTK_TABLE (table), label_to, 5, 6, 5, 6,
			  GTK_EXPAND | GTK_FILL,GTK_SHRINK | GTK_FILL,0,0);

	spin_to = gtk_spin_button_new_with_range (1, n_sheets, 1);
	gtk_table_attach (GTK_TABLE (table), spin_to, 6, 7, 5, 6,
			  GTK_EXPAND | GTK_FILL,GTK_SHRINK | GTK_FILL,0,0);
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin_to), n_sheets);

	g_signal_connect_after (G_OBJECT (button_selected_sheet), "toggled",
		G_CALLBACK (widget_button_cb), button_selection);
	g_signal_connect_after (G_OBJECT (button_selected_sheet), "toggled",
		G_CALLBACK (widget_button_cb), button_ignore_printarea);

	g_signal_connect_after (G_OBJECT (button_spec_sheets), "toggled",
		G_CALLBACK (widget_button_cb), label_from);
	g_signal_connect_after (G_OBJECT (button_spec_sheets), "toggled",
		G_CALLBACK (widget_button_cb), label_to);
	g_signal_connect_after (G_OBJECT (button_spec_sheets), "toggled",
		G_CALLBACK (widget_button_cb), spin_from);
	g_signal_connect_after (G_OBJECT (button_spec_sheets), "toggled",
		G_CALLBACK (widget_button_cb), spin_to);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button_selected_sheet), TRUE);

	settings =  gtk_print_operation_get_print_settings (operation);

	if (settings) {
		switch (gtk_print_settings_get_int_with_default
			(settings, GNUMERIC_PRINT_SETTING_PRINTRANGE_KEY,
			 PRINT_ACTIVE_SHEET)) {
		case PRINT_SHEET_SELECTION_IGNORE_PRINTAREA:
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button_ignore_printarea), TRUE);
			/* no break */
		case PRINT_SHEET_SELECTION:
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button_selection), TRUE);
			/* no break */
		case PRINT_ACTIVE_SHEET:
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button_selected_sheet), TRUE);
			break;
		case PRINT_IGNORE_PRINTAREA:
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button_ignore_printarea), TRUE);
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button_selected_sheet), TRUE);
			break;
		case PRINT_SHEET_RANGE:
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button_spec_sheets), TRUE);
			break;
		case PRINT_ALL_SHEETS:
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button_all_sheets), TRUE);
			break;
		}

		gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin_from),
					   gtk_print_settings_get_int_with_default
					   (settings, GNUMERIC_PRINT_SETTING_PRINT_FROM_SHEET_KEY,
					    1));
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin_to),
					   gtk_print_settings_get_int_with_default
					   (settings, GNUMERIC_PRINT_SETTING_PRINT_TO_SHEET_KEY,
					    n_sheets));
	}

	/* We are sending toggled signals to ensure that all widgets are */
	/* correctly enabled or disabled.                                */
	gtk_toggle_button_toggled (GTK_TOGGLE_BUTTON (button_selected_sheet));
	gtk_toggle_button_toggled (GTK_TOGGLE_BUTTON (button_spec_sheets));

	gtk_widget_show_all (frame);

	/* Let's save the widgets */
	pi->button_all_sheets = button_all_sheets;
	pi->button_selected_sheet = button_selected_sheet;
	pi->button_spec_sheets = button_spec_sheets;
	pi->button_selection = button_selection;
	pi->button_ignore_printarea = button_ignore_printarea;
	pi->spin_from = spin_from;
	pi->spin_to = spin_to;

	return G_OBJECT (frame);
}

static void
gnm_custom_widget_apply_cb (GtkPrintOperation *operation,
			    GtkWidget         *widget,
			    gpointer           user_data)
{
	PrintingInstance * pi = (PrintingInstance *) user_data;
	GtkPrintSettings * settings;
	PrintRange pr = PRINT_ACTIVE_SHEET;
	guint from, to;

	settings =  gtk_print_operation_get_print_settings (operation);

	g_return_if_fail (settings != NULL);

	from = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (pi->spin_from));
	to = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (pi->spin_to));

	gtk_print_settings_set_int (settings,
				    GNUMERIC_PRINT_SETTING_PRINT_FROM_SHEET_KEY,
				    from);
	gtk_print_settings_set_int (settings,
				    GNUMERIC_PRINT_SETTING_PRINT_TO_SHEET_KEY,
				    to);
	pi->from = from;
	pi->to = to;

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (pi->button_all_sheets))) {
		pr = PRINT_ALL_SHEETS;
	} else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (pi->button_spec_sheets))) {
		pr = PRINT_SHEET_RANGE;
	} else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (pi->button_selected_sheet))) {
		gboolean ignore_printarea = gtk_toggle_button_get_active
			(GTK_TOGGLE_BUTTON (pi->button_ignore_printarea));
		gboolean selection = gtk_toggle_button_get_active
			(GTK_TOGGLE_BUTTON (pi->button_selection));
		if (selection && ignore_printarea)
			pr = PRINT_SHEET_SELECTION_IGNORE_PRINTAREA;
		else if (selection)
			pr = PRINT_SHEET_SELECTION;
		else if (ignore_printarea)
			pr = PRINT_IGNORE_PRINTAREA;
		else
			pr = PRINT_ACTIVE_SHEET;
	}

	gtk_print_settings_set_int (settings,
				    GNUMERIC_PRINT_SETTING_PRINTRANGE_KEY, pr);
	pi->pr = pr;
}

void
gnm_print_sheet (WorkbookControl *wbc, Sheet *sheet,
		 gboolean preview, PrintRange default_range,
		 GsfOutput *export_dst)
{
  GtkPrintOperation *print;
  GtkPrintOperationResult res;
  GtkPageSetup *page_setup;
  PrintingInstance *pi;
  GtkPrintSettings* settings;
  GtkWindow *parent;
  GtkPrintOperationAction action;
  gchar *tmp_file_name = NULL;
  int tmp_file_fd = -1;

  print = gtk_print_operation_new ();

  pi = printing_instance_new ();
  pi->wb = sheet->workbook;
  pi->wbc = wbc ? WORKBOOK_CONTROL (wbc) : NULL;
  pi->sheet = sheet;

  gnm_gconf_init_printer_defaults ();

  settings = gnm_gconf_get_print_settings ();
  gtk_print_settings_set_int (settings, GNUMERIC_PRINT_SETTING_PRINTRANGE_KEY,
			      default_range);
  pi->pr = default_range;
  gtk_print_settings_set_use_color (settings, !sheet->print_info->print_black_and_white);
  gtk_print_operation_set_print_settings (print, settings);

  page_setup = print_info_get_page_setup (sheet->print_info);
  if (page_setup) {
	  gtk_print_operation_set_default_page_setup (print, page_setup);
	  g_object_unref (page_setup);
  }

  g_signal_connect (print, "begin-print", G_CALLBACK (gnm_begin_print_cb), pi);
  g_signal_connect (print, "paginate", G_CALLBACK (gnm_paginate_cb), pi);
  g_signal_connect (print, "draw-page", G_CALLBACK (gnm_draw_page_cb), pi);
  g_signal_connect (print, "end-print", G_CALLBACK (gnm_end_print_cb), pi);
  g_signal_connect (print, "request-page-setup", G_CALLBACK (gnm_request_page_setup_cb), pi);

  gtk_print_operation_set_use_full_page (print, FALSE);
  gtk_print_operation_set_unit (print, GTK_UNIT_POINTS);

  if (export_dst) {
	  GError *err = NULL;

	  tmp_file_fd = g_file_open_tmp ("pdfXXXXXX", &tmp_file_name, &err);
	  if (err) {
		  gsf_output_set_error (export_dst, 0, err->message);
		  g_error_free (err);
		  goto out;
	  }
	  gtk_print_operation_set_export_filename (print, tmp_file_name);

	  action = GTK_PRINT_OPERATION_ACTION_EXPORT;
	  parent = NULL;
	  gtk_print_operation_set_show_progress (print, FALSE);
  } else {
	  if (NULL != wbc && IS_WBC_GTK(wbc))
		  parent = wbcg_toplevel (WBC_GTK (wbc));
	  else
		  parent = NULL;
	  action = preview
		  ? GTK_PRINT_OPERATION_ACTION_PREVIEW
		  : GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG;
	  gtk_print_operation_set_show_progress (print, TRUE);
	  gtk_print_operation_set_custom_tab_label (print, _("Gnumeric Print Range"));
	  g_signal_connect (print, "create-custom-widget", G_CALLBACK (gnm_create_widget_cb), pi);
	  g_signal_connect (print, "custom-widget-apply", G_CALLBACK (gnm_custom_widget_apply_cb), pi);
  }

  res = gtk_print_operation_run (print, action, parent, NULL);

  switch (res) {
  case GTK_PRINT_OPERATION_RESULT_APPLY:
	  gnm_gconf_set_print_settings (gtk_print_operation_get_print_settings (print));
	  break;
  case GTK_PRINT_OPERATION_RESULT_CANCEL:
	  printing_instance_delete (pi);
	  break;
  case GTK_PRINT_OPERATION_RESULT_ERROR:
	  /* FIXME? */
	  break;
  case GTK_PRINT_OPERATION_RESULT_IN_PROGRESS:
	  /* FIXME? */
	  break;
  default: ;
  }

  if (tmp_file_name) {
	  char buffer[64 * 1024];
	  size_t bytes_read;

	  if (lseek (tmp_file_fd, 0, SEEK_SET) < 0)
		  bytes_read = -1;
	  else {
		  while ((bytes_read = read (tmp_file_fd, buffer, sizeof (buffer))) > 0) {
			  gsf_output_write (export_dst, bytes_read, buffer);
		  }
	  }
	  if (bytes_read < 0) {
		  int save_errno = errno;
		  if (!gsf_output_error (export_dst))
			  gsf_output_set_error (export_dst,
						g_file_error_from_errno (save_errno),
						g_strerror (save_errno));
	  }
  }

 out:
  if (tmp_file_fd >= 0)
	  close (tmp_file_fd);
  if (tmp_file_name) {
	  g_unlink (tmp_file_name);
	  g_free (tmp_file_name);
  }

  g_object_unref (print);
}

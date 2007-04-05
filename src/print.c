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
#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>
#include "print-cell.h"

#include "gnumeric.h"
#include "print.h"

#include "gui-util.h"
#include "sheet-object.h"
#include "sheet-object-impl.h"
#include "selection.h"
#include "workbook.h"
#include "workbook-control.h"
#include "workbook-edit.h"
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
#include "style-font.h"
#include "gnumeric-gconf.h"
#include <goffice/utils/go-font.h>


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
} PrintingInstance;

typedef struct {
	Sheet *sheet;
	gboolean selection;
        gboolean ignore_printarea;	
} SheetPrintInfo;


static PrintingInstance *
printing_instance_new (void) {
	PrintingInstance * pi = g_new0 (PrintingInstance,1);

	return pi;
}

static void
pi_free (gpointer data, gpointer user_data) {
	g_free(data);
}

static void
printing_instance_delete (PrintingInstance *pi) {
	g_list_foreach (pi->gnmSheets, pi_free, NULL);
	g_list_free (pi->gnmSheets);
	g_list_foreach (pi->gnmSheetRanges, pi_free, NULL);
	g_list_free (pi->gnmSheetRanges);
	g_free (pi);
}



#if 0

/*
 * Margins
 *
 * In the internal format, top and bottom margins, header and footer have
 * the same semantics as in Excel.  That is:
 *
 * +----------------------------------------+
 * |   ^            ^                       |
 * |   | top        |header                 |
 * |   |margin      v                       |
 * |-- |-------------                       |
 * |   v           header text              |
 * |------+--------------------------+------|
 * |      |                          |      |
 * |<---->|                          |<---->| ^
 * | left |           Cell           |right | |Increasing
 * |margin|                          |margin| |y
 * |      |           Grid           |      | |
 *
 * ~      ~                          ~      ~
 *
 * |      |                          |      |
 * |------+--------------------------+------|
 * |   ^           footer text              |
 * |-- |------------------------------------|
 * |   |bottom      ^                       |
 * |   |margin      |footer                 |
 * |   v            v                       |
 * +----------------------------------------+
 *
 * In the GUI on the other hand, top margin means the empty space above the
 * header text, and header area means the area from the top of the header
 * text to the top of the cell grid.  */

typedef struct {
	/*
	 * Part 1: The information the user configures on the Print Dialog
	 */
	PrintRange range;
	int start_page, end_page; /* Interval */
	gboolean sorted_print;
	gboolean is_preview;

	int current_output_sheet;		/* current sheet number during output */

	/*
	 * Part 2: Handy pre-computed information passed around
	 */
	double width, height;	/* total dimensions */
	double x_points;	/* real usable X (ie, width - margins) */
	double y_points;	/* real usable Y (ie, height - margins) */
	double titles_used_x;	/* points used by the X titles */
	double titles_used_y;	/* points used by the Y titles */

	/* The repeat columns/rows ranges used space */
	double repeat_cols_used_x;
	double repeat_rows_used_y;

	/*
	 * Part 3: Handy pointers
	 */
	GnomePrintContext *print_context;

	/*
	 * Part 5: Headers and footers
	 */
	HFRenderInfo *render_info;
	PangoLayout *decoration_layout;

	/* 6: The config */
	GnomePrintConfig *gp_config;
} PrintJobInfo;

static void
print_titles (PrintJobInfo const *pj, Sheet const *sheet, GnmRange *range,
	      double base_x, double base_y)
{
#warning TODO
}

#endif

static void
print_sheet_objects (GtkPrintContext   *context,
		     cairo_t *cr,
		     Sheet const *sheet,
		     GnmRange *range,
		     double base_x, double base_y)
{
	GSList *ptr;
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

	for (ptr = sheet->sheet_objects; ptr; ptr = ptr->next) {
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
	cairo_restore (cr);
}

static void
print_page_cells (GtkPrintContext   *context, PrintingInstance * pi,
		  cairo_t *cr, Sheet const *sheet, GnmRange *range,
		  double base_x, double base_y)
{
	PrintInformation const *pinfo = sheet->print_info;
	double width, height;

	/* Make sure the printing doesn't go beyond the specified cells. */
	cairo_save (cr);

	height = sheet_row_get_distance_pts (sheet, range->start.row,
					    range->end.row + 1);
	width = sheet_col_get_distance_pts (sheet,
					     range->start.col, range->end.col + 1);

/*      The next 8 lines create a huge performance hit. We have to figure out why that is */
/*      Not that we are essentially not using this since we have additional clip paths    */
/*      defined later but calculating the intersection of those paths seems time consuming */

 	if (sheet->text_is_rtl) { 
		base_x += gtk_print_context_get_width (context);
/* 		cairo_rectangle (cr, */
/* 				 base_x - width, base_y, */
/* 				 width, height); */
	}
/* 	else */
/* 		cairo_rectangle (cr, */
/* 				 base_x, base_y, */
/* 				 width, height); */
/* 	cairo_clip (cr); */

	gnm_gtk_print_cell_range (context, cr, sheet, range,
				  base_x, base_y, !pinfo->print_grid_lines);
	print_sheet_objects (context, cr, sheet, range, base_x, base_y);

	cairo_restore (cr);
}

#if 0

/*
 * print_page_repeated_rows
 *
 * It is up to the caller to determine if repeated rows should be printed on
 * the page.
 */
static void
print_page_repeated_rows (PrintJobInfo const *pj, Sheet const *sheet,
			  int start_col, int end_col,
			  double base_x, double base_y)
{
	PrintInformation const *pi = sheet->print_info;
	GnmRange const *r = &pi->repeat_top.range;
	GnmRange range;

	range_init (&range, start_col, MIN (r->start.row, r->end.row),
			    end_col,   MAX (r->start.row, r->end.row));
	//print_page_cells (pj, sheet, &range, base_x, base_y);
}


/*
 * print_page_repeated_cols
 *
 * It is up to the caller to determine if repeated columns should be printed
 * on the page.
 */
static void
print_page_repeated_cols (PrintJobInfo const *pj, Sheet const *sheet,
			  int start_row, int end_row,
			  double base_x, double base_y)
{
	PrintInformation const *pi = sheet->print_info;
	GnmRange const *r = &pi->repeat_left.range;
	GnmRange range;

	range_init (&range, MIN (r->start.col, r->end.col), start_row,
			    MAX (r->start.col, r->end.col), end_row);
	//print_page_cells (pj, sheet, &range, base_x, base_y);
}

/*
 * print_page_repeated_intersect
 *
 * Print the corner where repeated rows and columns intersect.
 *
 * It is impossible to print both from rows and columns. XL prints the cells
 * from the rows, whether order is row and column major. We do the same.
 */
static void
print_page_repeated_intersect (PrintJobInfo const *pj, Sheet const *sheet,
			       double base_x, double base_y,
			       double print_width, double print_height)
{
	PrintInformation const *pi = sheet->print_info;
	print_page_repeated_rows (pj, sheet,
				  pi->repeat_left.range.start.col,
				  pi->repeat_left.range.end.col,
				  base_x, base_y);
}

static PangoLayout *
ensure_decoration_layout (PrintJobInfo *pj)
{
	if (!pj->decoration_layout) {
		PangoLayout *layout = gnome_print_pango_create_layout (pj->print_context);
		/*
		 * Copy the style so we don't leave a cached GnmFont in the
		 * prefs object.
		 */
		GnmStyle *style = gnm_style_dup (gnm_app_prefs->printer_decoration_font);
		GnmFont *font = gnm_style_get_font
			(style,
			 pango_layout_get_context (layout),
			 1.);

		pj->decoration_layout = layout;
		pango_layout_set_font_description (layout, font->go.font->desc);
		gnm_style_unref (style);
	}
	return pj->decoration_layout;
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
 * Position at y, and clip to rectangle. If print_debugging is > 0, display
 * the rectangle.
 */
static void
print_hf_element (PrintJobInfo const *pj, Sheet const *sheet, char const *format,
		  PangoAlignment side, double y, gboolean align_bottom)
{
	PrintInformation const *pi = sheet->print_info;
	char *text;

	g_return_if_fail (pj != NULL);
	g_return_if_fail (pj->render_info != NULL);

	if (format == NULL)
		return;
	text = hf_format_render (format, pj->render_info, HF_RENDER_PRINT);

	if (text && text[0]) {
		double header = 0, footer = 0, left = 0, right = 0;
		PangoLayout *layout = ensure_decoration_layout ((PrintJobInfo *)pj);

		print_info_get_margins (pi, &header, &footer, &left, &right);
		pango_layout_set_alignment (layout, side);
		pango_layout_set_width (layout, (pj->width - left - right) * PANGO_SCALE);
		pango_layout_set_text (layout, text, -1);

		if (align_bottom) {
			int height;
			pango_layout_get_size (layout, NULL, &height);
			y += height / (double)PANGO_SCALE;
		}

		gnome_print_moveto (pj->print_context, left, y);
		gnome_print_pango_layout (pj->print_context, layout);
	}
	g_free (text);
}

/*
 * print_hf_line
 * @pj:     printing context
 * @hf:     header/footer descriptor
 * @y:      vertical position
 * @left:   left coordinate of clip rectangle
 * @bottom: bottom coordinate of clip rectangle
 * @right:  right coordinate of clip rectangel
 * @top:    top coordinate of clip rectangle
 *
 * Print a header/footer line.
 *
 * Position at y, and clip to rectangle. If print_debugging is > 0, display
 * the rectangle.
 */
static void
print_hf_line (PrintJobInfo const *pj, Sheet const *sheet,
	       PrintHF const *hf,
	       double y,
	       double left, double bottom, double right, double top,
	       gboolean align_bottom)
{
	/* Check if there's room to print. top and bottom are on the clip
	 * path, so we are actually requiring room for a 6x4 pt
	 * character. */
	if (ABS (top - bottom) < 8)
		return;
	if (ABS (left - right) < 6)
		return;

	gnome_print_gsave (pj->print_context);

	gnome_print_setrgbcolor (pj->print_context, 0, 0, 0);

	gnm_print_make_rect_path (pj->print_context,
				   left, bottom, right, top);

#ifndef NO_DEBUG_PRINT
	if (print_debugging > 0) {
		static const double dash[] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
		static gint n_dash = G_N_ELEMENTS (dash);

		gnome_print_gsave (pj->print_context);
		gnome_print_setdash (pj->print_context, n_dash, dash, 0.0);
		gnome_print_stroke  (pj->print_context);
		gnome_print_grestore (pj->print_context);
	}
#endif
	/* Clip the header or footer */
	gnome_print_clip      (pj->print_context);

	print_hf_element (pj, sheet, hf->left_format, PANGO_ALIGN_LEFT, y, align_bottom);
	print_hf_element (pj, sheet, hf->middle_format, PANGO_ALIGN_CENTER, y, align_bottom);
	print_hf_element (pj, sheet, hf->right_format, PANGO_ALIGN_RIGHT, y, align_bottom);

	gnome_print_grestore (pj->print_context);
}

/*
 * print_headers
 * @pj: printing context
 * Print headers
 *
 * Align ascenders flush with inside of top margin.
 */
static void
print_headers (PrintJobInfo const *pj, Sheet const *sheet)
{
	PrintInformation const *pi = sheet->print_info;
	PrintMargins const *pm = &pi->margin;
	double top, bottom, y;
	double header = 0, footer = 0, left = 0, right = 0;

	print_info_get_margins (pi, &header, &footer, &left, &right);

	y = pj->height - header;
	top    =  1 + pj->height - MIN (header, pm->top.points);
	bottom = -1 + pj->height - MAX (header, pm->top.points);

	print_hf_line (pj, sheet, pi->header, y,
		       -1 + left, bottom,
		       pj->width - right, top,
		       FALSE);
}

/*
 * print_footers
 * @pj: printing context
 * Print footers
 *
 * Align descenders flush with inside of bottom margin.
 */
static void
print_footers (PrintJobInfo const *pj, Sheet const *sheet)
{
	PrintInformation const *pi = sheet->print_info;
	PrintMargins const *pm = &pi->margin;
	double top, bottom, y;
	double header = 0, footer = 0, left = 0, right = 0;

	print_info_get_margins (pi, &header, &footer, &left, &right);

	y = footer;
	top    =  1 + MAX (footer, pm->bottom.points);
	bottom = -1 + MIN (footer, pm->bottom.points);

	/* Clip path for the header
	 * NOTE : postscript clip paths exclude the border, gdk includes it.
	 */
	print_hf_line (pj, sheet, pi->footer, y,
		       -1 + left, bottom,
		       pj->width - right, top,
		       TRUE);
}

static void
setup_scale (PrintJobInfo const *pj, Sheet const *sheet)
{
	PrintInformation const *pi = sheet->print_info;
	double affine [6];
	double x_scale = pi->scaling.percentage.x / 100.;
	double y_scale = pi->scaling.percentage.y / 100.;

	art_affine_scale (affine, x_scale, y_scale);
	gnome_print_concat (pj->print_context, affine);
}

static GnmValue *
cb_range_empty (GnmCellIter const *iter, gpointer flags)
{
	return (iter->ci->visible && iter->ri->visible) ? VALUE_TERMINATE : NULL;
}

#endif

/**
 * print_page:
 * @pj:        printing context
 * @sheet:     the sheet to print
 * @range:     a range
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
	    Sheet const *sheet,
	    GnmRange *range,
	    gboolean output)
{
	PrintInformation const *pinfo = sheet->print_info;
/* 	PrintMargins const *pm = &pinfo->margin; */
/* 	/\* print_height/width are sizes of the regular grid, */
/* 	 * not including repeating rows and columns *\/ */
	double print_height, print_width;
	double repeat_cols_used_x = 0., repeat_rows_used_y = 0.;
	double x, y, clip_y;
	char *pagenotxt;
	gboolean printed;
	GSList *ptr;
	double header = 0, footer = 0, left = 0, right = 0;
	cairo_t *cr;
	PangoLayout *layout;
	gdouble width, text_height;
	gint layout_height;
	PangoFontDescription *desc;

	cr = gtk_print_context_get_cairo_context (context);
	width = gtk_print_context_get_width (context);
	text_height  = gtk_print_context_get_height (context);

	cairo_save (cr);

	/* 	/\* FIXME: Can col / row space calculation be factored out? *\/ */

/* 	/\* Space for repeated rows depends on whether we print them or not *\/ */
/* 	if (pinfo->repeat_top.use && */
/* 	    range->start.row > pinfo->repeat_top.range.start.row) { */
/* 		repeat_rows_used_y = pj->repeat_rows_used_y; */
/* 		/\* Make sure start_row never is inside the repeated range *\/ */
/* 		range->start.row = MAX (range->start.row, */
/* 				        pinfo->repeat_top.range.end.row + 1); */
/* 	} else */
		repeat_rows_used_y = 0;

/* 	/\* Space for repeated cols depends on whether we print them or not *\/ */
/* 	if (pinfo->repeat_left.use && */
/* 	    range->start.col > pinfo->repeat_left.range.start.col) { */
/* 		repeat_cols_used_x = pj->repeat_cols_used_x; */
/* 		/\* Make sure start_col never is inside the repeated range *\/ */
/* 		range->start.col = MAX (range->start.col, */
/* 				        pinfo->repeat_left.range.end.col + 1); */
/* 	} else */
		repeat_cols_used_x = 0;

	/* If there are no cells in the area check for spans */
/* 	printed = (NULL != sheet_foreach_cell_in_range ((Sheet *)sheet, */
/* 							CELL_ITER_IGNORE_NONEXISTENT, */
/* 							range->start.col, */
/* 							range->start.row, */
/* 							range->end.col, */
/* 							range->end.row, */
/* 							cb_range_empty, NULL)); */
/* 	if (!printed) { */
/* 		int i = range->start.row; */
/* 		for (; i <= range->end.row ; ++i) { */
/* 			ColRowInfo const *ri = sheet_row_get_info (sheet, i); */
/* 			if (ri->visible && */
/* 			    (NULL != row_span_get (ri, range->start.col) || */
/* 			     NULL != row_span_get (ri, range->end.col))) { */
/* 				printed = TRUE; */
/* 				break; */
/* 			} */
/* 		} */
/* 	} */
/* 	if (!printed && pinfo->print_even_if_only_styles) */
/* 		printed = sheet_style_has_visible_content (sheet, range); */

/* 	/\* Check for sheet objects if nothing has been found so far *\/ */
/* 	if (!printed) */
/* 		for (ptr = sheet->sheet_objects; ptr && !printed; ptr = ptr->next) { */
/* 			SheetObject *so = SHEET_OBJECT (ptr->data); */
/* 			printed = range_overlap (range, &so->anchor.cell_bound); */
/* 		} */

/* 	if (!output) */
/* 		return printed; */

/* 	if (!printed) */
/* 		return 0; */

	x = 0.;
	y = 0.;

	print_height = sheet_row_get_distance_pts (sheet, range->start.row,
						   range->end.row + 1);
/* 	if (pinfo->center_vertically){ */
/* 		double h = print_height; */

/* 		if (pinfo->print_titles) */
/* 			h += sheet->rows.default_style.size_pts; */
/* 		h += repeat_rows_used_y; */
/* 		h *= pinfo->scaling.percentage.y / 100.; */
/* 		y = (pj->y_points - h)/2; */
/* 	} */

	print_width = sheet_col_get_distance_pts (sheet, range->start.col,
						  range->end.col + 1);
/* 	if (pinfo->center_horizontally){ */
/* 		double w = print_width; */

/* 		if (pinfo->print_titles) */
/* 			w += sheet->cols.default_style.size_pts; */
/* 		w += repeat_cols_used_x; */
/* 		w *= pinfo->scaling.percentage.x / 100.; */
/* 		x = (pj->x_points - w)/2; */
/* 	} */

/* 	print_info_get_margins (pinfo, &header, &footer, &left, &right); */
	/* Margins */
/* 	x += left; */
/* 	y += MAX (pm->top.points, header); */
/* 	if (pinfo->print_grid_lines) { */
/* 		/\* the initial grid lines *\/ */
/* 		x += 1.; */
/* 		y += 1.; */
/* 	} else { */
		/* If there are no grids alias back to avoid a penalty for the
		 * margins of the leading gridline */
		x -= GNM_COL_MARGIN;
		y -= GNM_ROW_MARGIN;
/* 	} */

/* 	/\* Note: we cannot have spaces in page numbers.  *\/ */
/* 	pagenotxt = hf_format_render (_("&[PAGE]"), */
/* 				      pj->render_info, HF_RENDER_PRINT); */
/* 	if (!pagenotxt) */
/* 		pagenotxt = g_strdup_printf ("%d", pj->render_info->page); */
/* 	gnome_print_beginpage (pj->print_context, pagenotxt); */
/* 	g_free (pagenotxt); */

/* 	print_headers (pj, sheet); */
/* 	print_footers (pj, sheet); */

/* 	/\* */
/* 	 * Print any titles that might be used */
/* 	 *\/ */
/* 	if (pinfo->print_titles) { */
/* 		print_titles (pj, sheet, range, x, y); */
/* 		x += sheet->cols.default_style.size_pts; */
/* 		y += sheet->rows.default_style.size_pts; */
/* 	} */

/* 	/\* Start a new path because the background fill function does not *\/ */
/* 	gnome_print_newpath (pj->print_context); */

/* 	setup_scale (pj, sheet); */
/* 	x /= pinfo->scaling.percentage.x / 100.; */
/* 	y /= pinfo->scaling.percentage.y / 100.; */

/* 	if (pinfo->repeat_top.use && repeat_rows_used_y > 0.) { */
/* 		/\* Intersection of repeated rows and columns *\/ */
/* 		if (pinfo->repeat_left.use && repeat_cols_used_x > 0.) */
/* 			print_page_repeated_intersect (pj, sheet, */
/* 				x, y, repeat_cols_used_x, repeat_rows_used_y); */

/* 		print_page_repeated_rows (pj, sheet, range->start.col, */
/* 					  range->end.col, */
/* 					  x + repeat_cols_used_x, y); */
/* 		y += repeat_rows_used_y; */
/* 	} */

/* 	if (pinfo->repeat_left.use && repeat_cols_used_x > 0. ) { */
/* 		print_page_repeated_cols (pj, sheet, range->start.row, */
/* 					  range->end.row, x, y); */
/* 		x += repeat_cols_used_x; */
/* 	} */

		print_page_cells (context, pi, cr, sheet, range, x, y);
		
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

#if 0

/* computer_scale_fit_to
 * Computes the scaling needed to fit all the rows or columns into the @usable
 * area.
 * This function is called when printing, and the user has selected the 'fit-to'
 * printing option. It will adjust the internal x and y scaling values to
 * make the sheet fit the desired number of pages, as suggested by the user.
 * It will only reduce the scaling to fit inside a page, not enlarge.
 */
static double
compute_scale_fit_to (PrintJobInfo const *pj, Sheet const *sheet,
		      int start, int end, double usable,
		      ColRowInfo const *(get_info)(Sheet const *sheet, int const p),
		      gint pages)
{
	double size_pts; /* The initial grid line on each page*/
	int idx;
	double scale;
	double max_unit = 0;

	/* This means to take whatever space is needed.  */
	if (pages <= 0)
		return 100;

	size_pts =  1. * pages;

	/* Work how much space the sheet requires. */
	for (idx = start; idx <= end; idx++) {
		ColRowInfo const *info = (*get_info) (sheet, idx);
		if (info->visible) {
			size_pts += info->size_pts;
			if (info->size_pts > max_unit)
				max_unit = (info->size_pts > usable) ?
					usable : info->size_pts;
		}
	}

	usable *= pages; /* Our usable area is this big. */

	/* What scale is required to fit the sheet onto this usable area? */
	/* Note that on each page but the last we may loose space that can */
	/* be nearly as large as the largest unit. */
	scale = usable / (size_pts + (pages - 1) * max_unit) * 100.;

	/* If the sheet needs to be shrunk, we update the scale.
	 * If it already fits, we simply leave the scale at 100.
	 * Another feature might be to enlarge a sheet so that it fills
	 * the page. But this is not a requested feature yet.
	 */
	return (scale < 100.) ? scale : 100.;
}

#endif

#define COL_FIT(col) (col >= SHEET_MAX_COLS ? (SHEET_MAX_COLS-1) : col)
#define ROW_FIT(row) (row >= SHEET_MAX_ROWS ? (SHEET_MAX_ROWS-1) : row)

#if 0

static double
print_range_used_units (Sheet const *sheet, gboolean compute_rows,
			PrintRepeatRange const *range)
{
	GnmRange const *r = &range->range;
	if (compute_rows)
		return sheet_row_get_distance_pts
			(sheet, r->start.row, r->end.row+1);
	else
		return sheet_col_get_distance_pts
			(sheet, r->start.col, r->end.col+1);
}

static void
print_job_info_init_sheet (PrintJobInfo *pj, Sheet const *sheet)
{
	PrintInformation const *pi = sheet->print_info;
	PrintMargins const *pm = &sheet->print_info->margin;
	double header = 0, footer = 0, left = 0, right = 0;

	if (!gnome_print_config_get_page_size (pj->gp_config, &pj->width, &pj->height))
		pj->width = pj->height = 1.;

	print_info_get_margins (pi, &header, &footer, &left, &right);
	pj->x_points = pj->width - (left + right);
	pj->y_points = pj->height -
		(MAX (pm->top.points, header) +
		 MAX (pm->bottom.points, footer));

	if (pi->print_titles) {
		pj->titles_used_x = sheet->cols.default_style.size_pts;
		pj->titles_used_y = sheet->rows.default_style.size_pts;
	} else {
		pj->titles_used_x = 0;
		pj->titles_used_y = 0;
	}

	pj->repeat_rows_used_y = (pi->repeat_top.use)
	    ? print_range_used_units (sheet, TRUE, &pi->repeat_top)
	    : 0.;
	pj->repeat_cols_used_x = (pi->repeat_left.use)
	    ? print_range_used_units (sheet, FALSE, &pi->repeat_left)
	    : 0.;

	pj->render_info->sheet = sheet;
}

/*
 * code to count the number of pages that will be printed.
 * Unfortuantely a lot of data here is calculated again when you
 * actually print the page ...
 */

typedef struct _PageCountInfo {
	int pages;
	PrintJobInfo *pj;
	GnmRange r;
	int current_output_sheet;
} PageCountInfo;

#endif

static void
compute_sheet_pages_add_sheet (PrintingInstance * pi, Sheet const *sheet, gboolean selection,
                     gboolean ignore_printarea)
{
	SheetPrintInfo *spi = g_new (SheetPrintInfo, 1);
	spi->sheet = sheet;
	spi->selection = selection;
	spi->ignore_printarea = ignore_printarea;
	pi->gnmSheets = g_list_append(pi->gnmSheets, spi);
}

static void
compute_sheet_pages_add_range (PrintingInstance * pi, Sheet const *sheet,
			       GnmRange *r)
{
		GnmSheetRange *gsr = g_new (GnmSheetRange,1);

		gsr->range = *r;
		gsr->sheet = (Sheet *) sheet;
		pi->gnmSheetRanges = g_list_append(pi->gnmSheetRanges, gsr);	
}


static void
compute_sheet_pages_across_then_down (GtkPrintContext   *context,
				      PrintingInstance * pi,
				      Sheet const *sheet,
				      GnmRange const *r)
{
/* 	PrintInformation *pi = sheet->print_info; */
	double usable_x, usable_x_initial, usable_x_repeating;
	double usable_y, usable_y_initial, usable_y_repeating;
	int row = r->start.row;
	gboolean printed = TRUE;

/* 	usable_x_initial   = pj->x_points - pj->titles_used_x; */
/* 	usable_x_repeating = usable_x_initial - pj->repeat_cols_used_x; */
/* 	usable_y_initial   = pj->y_points - pj->titles_used_y; */
/* 	usable_y_repeating = usable_y_initial - pj->repeat_rows_used_y; */

	usable_x_initial   = gtk_print_context_get_width (context);
	usable_x_repeating = usable_x_initial;
	usable_y_initial   = gtk_print_context_get_height (context);
	usable_y_repeating = usable_y_initial;

/* 	if (pi->scaling.type == PRINT_SCALE_FIT_PAGES) { */
/* 		int col = r->start.col; */
/* 		int row = r->start.row; */

/* 		if (col < pi->repeat_left.range.end.col) { */
/* 			usable_x = usable_x_initial; */
/* 			col = MIN (col, pi->repeat_left.range.end.col); */
/* 		} else */
/* 			usable_x = usable_x_repeating; */
/* 		pi->scaling.percentage.x = compute_scale_fit_to (pj, sheet, col, r->end.col, */
/* 			usable_x, sheet_col_get_info, pi->scaling.dim.cols); */

/* 		if (row < pi->repeat_top.range.end.row) { */
/* 			usable_y = usable_y_initial; */
/* 			row = MIN (row, pi->repeat_top.range.end.row); */
/* 		} else */
/* 			usable_y = usable_y_repeating; */
/* 		pi->scaling.percentage.y = compute_scale_fit_to (pj, sheet, row, r->end.row, */
/* 			usable_y, sheet_row_get_info, pi->scaling.dim.rows); */

/* 		if (pi->scaling.percentage.y > pi->scaling.percentage.x) */
/* 			pi->scaling.percentage.y = pi->scaling.percentage.x; */
/* 		else */
/* 			pi->scaling.percentage.x = pi->scaling.percentage.y; */
/* 	} */

	while (row <= r->end.row) {
		int row_count;
		int col = r->start.col;

/* 		if (row <= pi->repeat_top.range.end.row) { */
/* 			usable_y = usable_y_initial; */
/* 			row = MIN (row, pi->repeat_top.range.end.row); */
/* 		} else */
		usable_y = usable_y_repeating;

/* 		usable_y /= pi->scaling.percentage.y / 100.; */
		row_count = compute_group (sheet, row, r->end.row,
					   usable_y, sheet_row_get_info);

		while (col <= r->end.col) {
			GnmRange range;
			int col_count;

/* 			if (col <= pi->repeat_left.range.end.col) { */
/* 				usable_x = usable_x_initial; */
/* 				col = MIN (col, */
/* 					   pi->repeat_left.range.end.col); */
/* 			} else */
			usable_x = usable_x_repeating;

/* 			usable_x /= pi->scaling.percentage.x / 100.; */
			col_count = compute_group (sheet, col, r->end.col,
						   usable_x, sheet_col_get_info);
			range_init (&range, COL_FIT (col), ROW_FIT (row),
				    COL_FIT (col + col_count - 1),
				    ROW_FIT (row + row_count - 1));
/* 			printed = print_page (pj, sheet, &range, output); */

			col += col_count;

			if (printed)
				compute_sheet_pages_add_range (pi, sheet, &range);
		}
		row += row_count;
	}

	return;
}



static void
compute_sheet_pages (GtkPrintContext   *context,
		     PrintingInstance * pi,
		     Sheet const *sheet,
		     gboolean selection,
		     gboolean ignore_printarea)
{
	PrintInformation const *pinfo = sheet->print_info;
	GnmRange r;
	GnmRange const *selection_range;
	GnmRange print_area;

	print_area = sheet_get_printarea	(sheet,
					 pinfo->print_even_if_only_styles,
					 ignore_printarea);
	if (selection) {
		selection_range = selection_first_range
			(sheet_get_view (sheet, wb_control_view (pi->wbc)),
			  GO_CMD_CONTEXT (pi->wbc), _("Print Selection"));
	}

	if (selection && !ignore_printarea) {
		if (!selection_range || !range_intersection (&r, selection_range, &print_area))
			return;
	} else if (selection && ignore_printarea) {
		if (selection_range == NULL)
			return;
		r = *selection_range;
	} else
		r = print_area;
	
 	if (sheet->print_info->print_across_then_down)
		return compute_sheet_pages_across_then_down (context, pi, sheet, &r);
	else
/* 		return compute_sheet_pages_down_then_across (context, pi, sheet, &r, output); */
		return compute_sheet_pages_across_then_down (context, pi, sheet, &r);

	
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
	Workbook * wb = pi->wb;
	guint i;
	guint n;

	switch (pr) {
	case PRINT_ACTIVE_SHEET:
		compute_sheet_pages_add_sheet (pi,
			   pi->sheet, FALSE, FALSE);
		break;
	case PRINT_ALL_SHEETS:
		n =  workbook_sheet_count (wb);
		for (i = 0; i < n; i++)
			compute_sheet_pages_add_sheet (pi,
					     workbook_sheet_by_index (wb, i),
					     FALSE, FALSE);
		break;
	case PRINT_SHEET_RANGE:
		n =  workbook_sheet_count (wb);
		if (to > n)
			to = n;
		for (i = from - 1; i < to; i++)
			compute_sheet_pages_add_sheet (pi,
					     workbook_sheet_by_index (wb, i),
					     FALSE, FALSE);
		break;
	case PRINT_SHEET_SELECTION:
		compute_sheet_pages_add_sheet (pi,
				     pi->sheet, TRUE, FALSE);
		break;
	case PRINT_IGNORE_PRINTAREA:
		compute_sheet_pages_add_sheet (pi,
				     pi->sheet, FALSE, TRUE);
		break;
	case PRINT_SHEET_SELECTION_IGNORE_PRINTAREA:
		compute_sheet_pages_add_sheet (pi,
				     pi->sheet, TRUE, TRUE);
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
		if (n_pages == 0) /* gtk+ cannot handle 0 pages */
			n_pages = 1;
		
		gtk_print_operation_set_n_pages (operation, n_pages);
		gtk_print_operation_set_unit (operation, GTK_UNIT_POINTS);
		
		return TRUE;
	}
	
	compute_sheet_pages (context, pi, spi->sheet, spi->selection, spi->ignore_printarea);
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
		g_warning ("Working around gtk+ bug 423484.");
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
gnm_draw_page_cb (GtkPrintOperation *operation,
                  GtkPrintContext   *context,
		  gint               page_nr,
		  gpointer           user_data)
{

	PrintingInstance * pi = (PrintingInstance *) user_data;

	GnmSheetRange * gsr = g_list_nth_data (pi->gnmSheetRanges,
					       page_nr);
	if (gsr)
		print_page (operation, context, pi,
			    gsr->sheet, &(gsr->range), TRUE);	
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
gnm_print_sheet (WorkbookControlGUI *wbcg, Sheet *sheet,
		 gboolean preview, PrintRange default_range)
{

  GtkPrintOperation *print;
  GtkPrintOperationResult res;
  GtkPageSetup *page_setup;
  PrintingInstance *pi;
  GtkPrintSettings* settings;
  
  print = gtk_print_operation_new ();
  
  pi = printing_instance_new ();
  pi->wb = wb_control_get_workbook (WORKBOOK_CONTROL (wbcg));
  pi->wbc = WORKBOOK_CONTROL (wbcg);
  pi->sheet = sheet;

  gnm_gconf_init_printer_defaults ();

  settings = gnm_gconf_get_print_settings ();
  gtk_print_settings_set_int (settings, GNUMERIC_PRINT_SETTING_PRINTRANGE_KEY,
			      default_range);
  pi->pr = default_range;
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
  g_signal_connect (print, "create-custom-widget", G_CALLBACK (gnm_create_widget_cb), pi);
  g_signal_connect (print, "custom-widget-apply", G_CALLBACK (gnm_custom_widget_apply_cb), pi);

  gtk_print_operation_set_use_full_page (print, FALSE);
  gtk_print_operation_set_unit (print, GTK_UNIT_POINTS);
  gtk_print_operation_set_show_progress (print, TRUE);
  gtk_print_operation_set_custom_tab_label (print, _("Gnumeric Print Range"));

  res = gtk_print_operation_run (print,
				 preview ? GTK_PRINT_OPERATION_ACTION_PREVIEW
				         : GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG,
                                 wbcg_toplevel (wbcg), NULL);

  if (res == GTK_PRINT_OPERATION_RESULT_APPLY)
    	gnm_gconf_set_print_settings (gtk_print_operation_get_print_settings (print));

  g_object_unref (print);
}

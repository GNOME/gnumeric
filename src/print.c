/*
 * print.c: Printing routines for Gnumeric
 *
 * Authors:
 *    Miguel de Icaza (miguel@gnu.org)
 *    Morten Welinder (terra@gnome.org)
 *    Andreas J. Guelzow (aguelzow@pyrshep.ca)
 *
 * Copyright 2007, Andreas J. Guelzow, All Rights Reserved
 * Copyright (C) 2007-2009 Morten Welinder (terra@gnome.org)
 *
 * Handles printing of Sheets.
 */
#include <gnumeric-config.h>
#include <print-cell.h>

#include <gnumeric.h>
#include <print.h>

#include <gui-util.h>
#include <gutils.h>
#include <sheet-object.h>
#include <sheet-object-impl.h>
#include <selection.h>
#include <workbook.h>
#include <workbook-control.h>
#include <wbc-gtk.h>
#include <command-context.h>
#include <dialogs/dialogs.h>
#include <gnumeric-conf.h>
#include <libgnumeric.h>
#include <sheet.h>
#include <value.h>
#include <cellspan.h>
#include <print-info.h>
#include <application.h>
#include <sheet-style.h>
#include <ranges.h>
#include <parse-util.h>
#include <style-font.h>
#include <gnumeric-conf.h>
#include <goffice/goffice.h>

#include <gsf/gsf-meta-names.h>

#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>
#include <glib.h>

#include <unistd.h>
#include <errno.h>

#ifdef G_OS_WIN32
#include <windows.h>
/* see bug #533795. */
#define PREVIEW_VIA_PDF
#endif

/*  The following structure is used by the printing system */

typedef struct {
	GList *gnmSheets;
	Workbook *wb;
	WorkbookControl *wbc;
	Sheet *sheet;
	GtkWidget *button_all_sheets, *button_selected_sheet,
		*button_spec_sheets;
	GtkWidget *button_selection, *button_ignore_printarea, *button_print_hidden_sheets;
	GtkWidget *button_ignore_page_breaks;
	GtkWidget *spin_from, *spin_to;
	PrintRange pr;
	guint to, from;
	gboolean ignore_pb;
	guint last_pagination;
	GnmPrintHFRenderInfo *hfi;
	GtkWidget *progress;
	gboolean cancel;
	gboolean preview;
} PrintingInstance;

typedef struct {
	Sheet *sheet;
	gboolean selection;
        gboolean ignore_printarea;
	GArray *column_pagination;
	GArray *row_pagination;
	guint   pages;
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


GType
gnm_print_range_get_type (void)
{
	static GType etype = 0;
	if (etype == 0) {
		static GEnumValue const values[] = {
			{ GNM_PRINT_SAVED_INFO,
			  "GNM_PRINT_SAVED_INFO",
			  "as-saved"},
			{ GNM_PRINT_ACTIVE_SHEET,
			  "GNM_PRINT_ACTIVE_SHEET",
			  "active-sheet"},
			{ GNM_PRINT_ALL_SHEETS,
			  "GNM_PRINT_ALL_SHEETS",
			  "all-sheets"},
			{ GNM_PRINT_ALL_SHEETS_INCLUDING_HIDDEN,
			  "GNM_PRINT_ALL_SHEETS_INCLUDING_HIDDEN",
			  "all-sheets-incl-hidden"},
			{ GNM_PRINT_SHEET_RANGE,
			  "GNM_PRINT_SHEET_RANGE",
			  "sheet-range"},
			{ GNM_PRINT_SHEET_SELECTION,
			  "GNM_PRINT_SHEET_SELECTION",
			  "sheet-selection"},
			{ GNM_PRINT_IGNORE_PRINTAREA,
			  "GNM_PRINT_IGNORE_PRINTAREA",
			  "ignore-print-area"},
			{ GNM_PRINT_SHEET_SELECTION_IGNORE_PRINTAREA,
			  "GNM_PRINT_SHEET_SELECTION_IGNORE_PRINTAREA",
			  "sheet-selection-ignore-printarea"},
			{ 0, NULL, NULL }
		};
		etype = g_enum_register_static ("GnmPrintRange", values);
	}
	return etype;
}


static PrintingInstance *
printing_instance_new (void)
{
	PrintingInstance * pi = g_new0 (PrintingInstance,1);
	pi->hfi = gnm_print_hf_render_info_new ();
	pi->cancel = FALSE;
	pi->hfi->pages = -1;

	return pi;
}

static void
sheet_print_info_free (gpointer data)
{
	SheetPrintInfo *spi = data;

	g_array_unref (spi->column_pagination);
	g_array_unref (spi->row_pagination);
	g_free (data);
}

static void
printing_instance_delete (PrintingInstance *pi)
{
	g_list_free_full (pi->gnmSheets, sheet_print_info_free);
	gnm_print_hf_render_info_destroy (pi->hfi);
	if (pi->progress) {
		gtk_widget_destroy (pi->progress);
	}
	g_free (pi);
}

void
gnm_print_sheet_objects (cairo_t *cr,
			 Sheet const *sheet,
			 GnmRange *range,
			 double base_x, double base_y)
{
	GSList *ptr, *objects;
	double width, height;

	g_return_if_fail (IS_SHEET (sheet));
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
		SheetObject *so = GNM_SO (ptr->data);
		GnmRange const *r = &so->anchor.cell_bound;

		if (!sheet_object_can_print (so) ||
		    !range_overlap (range, &so->anchor.cell_bound))
			continue;

		cairo_save (cr);
		/* move to top left */
		if (sheet->text_is_rtl) {
			double tr_x, tr_y;
			switch (so->anchor.mode) {
			case GNM_SO_ANCHOR_ABSOLUTE:
				tr_x = base_x - 0.5; /* because of leading gridline */
				tr_y = base_y + 0.5;
				break;
			case GNM_SO_ANCHOR_ONE_CELL:
				tr_x = base_x - 0.5
					- sheet_col_get_distance_pts (sheet, 0, r->start.col+1)
					+ sheet_col_get_distance_pts (sheet, 0, range->start.col);
				tr_y = base_y + 0.5
					+ sheet_row_get_distance_pts (sheet, 0, r->start.row)
					- sheet_row_get_distance_pts (sheet, 0, range->start.row);
				break;
			default:
				tr_x = base_x - 0.5
					- sheet_col_get_distance_pts (sheet, 0, r->end.col+1)
					+ sheet_col_get_distance_pts (sheet, 0, range->start.col);
				tr_y = base_y + 0.5
					+ sheet_row_get_distance_pts (sheet, 0, r->start.row)
					- sheet_row_get_distance_pts (sheet, 0, range->start.row);
				break;
			}
			cairo_translate (cr, tr_x, tr_y);
		} else
			cairo_translate (cr, (so->anchor.mode == GNM_SO_ANCHOR_ABSOLUTE)?
					 base_x + 0.5: base_x + 0.5
					 + sheet_col_get_distance_pts (sheet, 0, r->start.col)
					 - sheet_col_get_distance_pts (sheet, 0,
								       range->start.col),
			         (so->anchor.mode == GNM_SO_ANCHOR_ABSOLUTE)?
					 base_y + 0.5: base_y + 0.5
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
print_page_cells (G_GNUC_UNUSED GtkPrintContext   *context,
		  G_GNUC_UNUSED PrintingInstance * pi,
		  cairo_t *cr, Sheet const *sheet, GnmRange *range,
		  double base_x, double base_y)
{
	gnm_gtk_print_cell_range (cr, sheet, range,
				  base_x, base_y,
				  (GnmPrintInformation const *) sheet->print_info);
	gnm_print_sheet_objects (cr, sheet, range, base_x, base_y);
}

static void
print_header_gtk (GtkPrintContext   *context, cairo_t *cr,
		  double x, double y, double w, double h,
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
print_page_col_headers (GtkPrintContext   *context,
			G_GNUC_UNUSED PrintingInstance * pi,
			cairo_t *cr, Sheet const *sheet, GnmRange *range,
			double row_header_width, double col_header_height)
{
	int start_col, end_col;
	int col;
	double x;
	PangoFontDescription *desc;
	double hscale;

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (range != NULL);
	g_return_if_fail (range->start.col <= range->end.col);

	hscale = sheet->display_formulas ? 2 : 1;
	desc = pango_font_description_from_string ("sans 12");

	start_col = range->start.col;
	end_col = range->end.col;

	x = (row_header_width + GNM_COL_MARGIN) * (sheet->text_is_rtl ? -1. : 1.);

	for (col = start_col; col <= end_col ; col++) {
		ColRowInfo const *ci = sheet_col_get_info (sheet, col);

		if (ci->visible) {
			if (sheet->text_is_rtl)
				x -= ci->size_pts * hscale;

			print_header_gtk (context, cr,
					  x + 0.5, 0,
					  ci->size_pts * hscale - 1,
					  col_header_height - 0.5,
					  col_name (col), desc);

			if (!sheet->text_is_rtl)
				x += ci->size_pts * hscale;
		}
	}

	pango_font_description_free (desc);
}

static void
print_page_row_headers (GtkPrintContext *context,
			G_GNUC_UNUSED PrintingInstance * pi,
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
ensure_decoration_layout (GtkPrintContext *context)
{
	GnmStyle *style;
	GnmFont *font;
	PangoLayout *layout;

	layout = gtk_print_context_create_pango_layout (context);
	style = gnm_conf_get_printer_decoration_font ();
	font = gnm_style_get_font
		(style, pango_layout_get_context (layout));
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
 * Position at y, and clip to rectangle.
 */
static void
print_hf_element (GtkPrintContext *context, cairo_t *cr,
		  G_GNUC_UNUSED Sheet const *sheet,
		  char const *format,
		  PangoAlignment side, gdouble width, gboolean align_bottom,
		  GnmPrintHFRenderInfo *hfi)
{
	PangoLayout *layout;

	gdouble text_height = 0.;
	char *text;

	if (format == NULL)
		return;

	text = gnm_print_hf_format_render (format, hfi, HF_RENDER_PRINT);

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
	       GnmPrintHF const *hf, gboolean align_bottom, gdouble width, GnmPrintHFRenderInfo *hfi)
{
	print_hf_element (context, cr, sheet, hf->left_format, PANGO_ALIGN_LEFT, width, align_bottom, hfi);
	print_hf_element (context, cr, sheet, hf->middle_format, PANGO_ALIGN_CENTER, width, align_bottom, hfi);
	print_hf_element (context, cr, sheet, hf->right_format, PANGO_ALIGN_RIGHT, width, align_bottom, hfi);
}



/**
 * print_page:
 * @pj:        printing context
 * @gsr:      the page information
 *
 * Excel prints repeated rows like this: Pages up to and including the page
 * where the first of the repeated rows "naturally" occurs are printed in
 * the normal way. On subsequent pages, repated rows are printed before the
 * regular flow.
 */
static gboolean
print_page (G_GNUC_UNUSED GtkPrintOperation *operation,
	    GtkPrintContext   *context,
	    PrintingInstance * pi,
	    SheetPageRange *gsr)
{
	Sheet *sheet = gsr->sheet;
	GnmPrintInformation *pinfo = sheet->print_info;
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
		cairo_translate (cr, 0, height + (edge_to_below_header - header) + (edge_to_above_footer - footer));
		print_hf_line (context, cr, sheet, pinfo->footer, TRUE, width,
			       pi->hfi);
		cairo_restore (cr);
	}

/* setting up content area */
	cairo_save (cr);
	cairo_translate (cr, sheet->text_is_rtl ? width : 0, edge_to_below_header - header);

	if (sheet->sheet_type == GNM_SHEET_OBJECT) {
		SheetObject *so = sheet->sheet_objects
			? sheet->sheet_objects->data
			: NULL;
		if (so) {
			cairo_scale (cr, px, py);
			sheet_object_draw_cairo_sized (so, cr, width, height);
		}
	} else {

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
	}

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

	if (count ==0) {
		g_warning (_("Even one cell is too large for this page."));

		/* If we do not return at least one we are going into an infinite loop! */
		return 1;
	}

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
paginate (GArray *paginationInfo,
	  Sheet const *sheet,
	  gint start, gint end,
	  gdouble usable, gboolean repeat, gint repeat_start, gint repeat_end,
	  double (sheet_get_distance_pts) (Sheet const *sheet, int from, int to),
	  ColRowInfo const *(get_info)(Sheet const *sheet, int const p),
	  GnmPageBreaks *pb, gboolean store_breaks)
{
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
		gint n_end;

		n_end = gnm_page_breaks_get_next_manual_break (pb, rc) - 1;
		if (n_end < rc)
			n_end = end;

		while (rc <= n_end) {
			int count;

			gdouble repeating_used = 0.;
			gint n_rep_used = 0, first_rep_used = 0;

			adjust_repetition (sheet, rc,
					   first_rep, n_rep,
					   repeating,
					   &first_rep_used, &n_rep_used,
					   &repeating_used,
					   sheet_get_distance_pts);

			count = compute_group (sheet, rc, n_end,
					       usable - repeating_used,
					       get_info);

			if (paginationInfo) {
				PaginationInfo item;
				item.rc = rc;
				item.count = count;
				item.first_rep = first_rep_used;
				item.n_rep = n_rep_used;
				g_array_append_val (paginationInfo, item);
			}
			page_count++;

			rc += count;
			if (store_breaks && (rc < n_end))
				gnm_page_breaks_set_break (pb, rc, GNM_PAGE_BREAK_AUTO);
		}
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
		      gboolean repeat, gint repeat_start, gint repeat_end, GnmPageBreaks *pb)
{
	double max_p, min_p;
	gint   max_pages;
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
	max_p = CLAMP (max_p, 0.01, max_percent);

	max_pages = paginate (NULL, sheet, start, end, usable/max_p - header,
			      repeat, repeat_start, repeat_end,
			      get_distance_pts, get_info, pb, FALSE);

	if (max_pages == pages)
		return max_p;

	/* The we calculate the min percentage */

	min_p = usable/(extent + header);
	min_p = CLAMP (min_p, 0.01, max_percent);

	paginate (NULL, sheet, start, end, usable/min_p - header,
		  repeat, repeat_start, repeat_end,
		  get_distance_pts, get_info, pb, FALSE);


	/* And then we pick the middle until the percentage is within 0.1% of */
	/* the desired percentage */

	while (max_p - min_p > 0.001) {
		double cur_p = (max_p + min_p) / 2.;
		int cur_pages = paginate (NULL, sheet, start, end, usable/cur_p - header,
					  repeat, repeat_start, repeat_end,
					  get_distance_pts, get_info, pb, FALSE);

		if (cur_pages > pages)
			max_p = cur_p;
		else
			min_p = cur_p;
	}

	return min_p;
}

#define COL_FIT(col) (MIN (col, gnm_sheet_get_last_col (sheet)))
#define ROW_FIT(row) (MIN (row, gnm_sheet_get_last_row (sheet)))

static void
compute_sheet_pages_add_sheet (PrintingInstance * pi, Sheet const *sheet,
			       gboolean selection,
			       gboolean ignore_printarea)
{
	SheetPrintInfo *spi = g_new0 (SheetPrintInfo, 1);

	spi->sheet = (Sheet *)sheet;
	spi->selection = selection;
	spi->ignore_printarea = ignore_printarea;
	pi->gnmSheets = g_list_append (pi->gnmSheets, spi);
}

static Sheet *
print_get_sheet (PrintingInstance *pi, guint page_no)
{
	GList *l;

	for (l = pi->gnmSheets; l != NULL; l = l->next) {
		SheetPrintInfo *spi = l->data;
		if (spi->pages > page_no)
			return spi->sheet;
		else
			page_no -= spi->pages;
	}

	return NULL;
}

static SheetPageRange *
print_get_sheet_page_range (PrintingInstance *pi, guint page_no)
{
	GList *l;

	for (l = pi->gnmSheets; l != NULL; l = l->next) {
		SheetPrintInfo *spi = l->data;
		if (spi->pages > page_no) {
			SheetPageRange *gsr;
			guint col, row;
			PaginationInfo *c_info, *r_info;
			Sheet *sheet = spi->sheet;

			if (sheet->print_info->print_across_then_down) {
				col = page_no % spi->column_pagination->len;
				row = page_no / spi->column_pagination->len;
			} else {
				col = page_no / spi->row_pagination->len;
				row = page_no % spi->row_pagination->len;
			}
			g_return_val_if_fail (col < spi->column_pagination->len &&
					      row < spi->row_pagination->len, NULL);
			gsr = g_new (SheetPageRange,1);
			c_info = &(g_array_index (spi->column_pagination,
						  PaginationInfo, col));
			r_info = &(g_array_index (spi->row_pagination,
						  PaginationInfo, row));
			range_init (&gsr->range,
				    COL_FIT (c_info->rc), ROW_FIT (r_info->rc),
				    COL_FIT (c_info->rc + c_info->count - 1),
				    ROW_FIT (r_info->rc + r_info->count - 1));
			gsr->n_rep_cols = c_info->n_rep;
			gsr->first_rep_cols = c_info->first_rep;
			gsr->n_rep_rows = r_info->n_rep;
			gsr->first_rep_rows = r_info->first_rep;
			gsr->sheet = sheet;
			return gsr;
		} else
			page_no -= spi->pages;
	}

	return NULL;
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
	GnmPrintInformation *pinfo = sheet->print_info;
	GnmRange r;
	GnmRange const *selection_range;
	GnmRange print_area;
	gdouble col_header_height = 0.;
	gdouble row_header_width = 0.;
	gdouble page_width, page_height;
	gdouble top_margin, bottom_margin, edge_to_below_header, edge_to_above_footer;
	gdouble px, py;
	gdouble usable_x, usable_y;
	GArray *column_pagination = g_array_sized_new
		(FALSE, TRUE, sizeof (PaginationInfo), 100);
	GArray *row_pagination = g_array_sized_new
		(FALSE, TRUE, sizeof (PaginationInfo), 100);
	gboolean repeat_top_use, repeat_left_use;
	int repeat_top_start, repeat_top_end, repeat_left_start, repeat_left_end;
	double const hscale = sheet->display_formulas ? 2 : 1;

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
			print_area = *selection_range;
		} else {
			if (!range_intersection (&r, selection_range, &print_area))
				return FALSE;
			print_area = r;
		}
	};

	page_width = gtk_print_context_get_width (context);
	page_height = gtk_print_context_get_height (context);
	print_info_get_margins (pinfo, &top_margin, &bottom_margin, NULL, NULL,
				&edge_to_below_header, &edge_to_above_footer);
	page_height -= ((edge_to_below_header - top_margin)
			+ (edge_to_above_footer - bottom_margin));

	repeat_top_use = print_load_repeat_range (pinfo->repeat_top, &r, sheet);
	repeat_top_start = repeat_top_use ? r.start.row : 0;
	repeat_top_end = repeat_top_use ? r.end.row : 0;

	repeat_left_use = print_load_repeat_range (pinfo->repeat_left, &r, sheet);
	repeat_left_start = repeat_left_use ? r.start.col : 0;
	repeat_left_end = repeat_left_use ? r.end.col : 0;

	if (!pi->ignore_pb) {
		if (pinfo->page_breaks.h == NULL)
		        print_info_set_breaks (pinfo,
					       gnm_page_breaks_new (FALSE));
		else
			gnm_page_breaks_clean (pinfo->page_breaks.h);
		if (pinfo->page_breaks.v == NULL)
		        print_info_set_breaks (pinfo,
					       gnm_page_breaks_new (TRUE));
		else
			gnm_page_breaks_clean (pinfo->page_breaks.v);

	}

	if (pinfo->scaling.type == PRINT_SCALE_FIT_PAGES) {
		/* Note that the resulting scale is independent from */
		/* whether we print first down or across!            */
		gdouble pxy;

		pxy = compute_scale_fit_to (sheet, print_area.start.row, print_area.end.row,
					    page_height, sheet_row_get_info,
					    sheet_row_get_distance_pts,
					    pinfo->scaling.dim.rows, 1.,
					    col_header_height,
					    repeat_top_use,
					    repeat_top_start, repeat_top_end,
					    pi->ignore_pb ? NULL : pinfo->page_breaks.h);
		pxy = compute_scale_fit_to (sheet, print_area.start.col, print_area.end.col,
					    page_width, sheet_col_get_info,
					    sheet_col_get_distance_pts,
					    pinfo->scaling.dim.cols, pxy,
					    row_header_width,
					    repeat_left_use,
					    repeat_left_start, repeat_left_end,
					    pi->ignore_pb ? NULL : pinfo->page_breaks.v);

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

	paginate (column_pagination, sheet, print_area.start.col, print_area.end.col,
		  (usable_x - row_header_width)/hscale,
		  repeat_left_use, repeat_left_start, repeat_left_end,
		  sheet_col_get_distance_pts, sheet_col_get_info,
		  pi->ignore_pb ? NULL : pinfo->page_breaks.v, !pi->ignore_pb);
	paginate (row_pagination, sheet, print_area.start.row, print_area.end.row,
		  usable_y - col_header_height,
		  repeat_top_use, repeat_top_start, repeat_top_end,
		  sheet_row_get_distance_pts, sheet_row_get_info,
		  pi->ignore_pb ? NULL : pinfo->page_breaks.h, !pi->ignore_pb);

	spi->column_pagination = column_pagination;
	spi->row_pagination = row_pagination;
	spi->pages = column_pagination->len * row_pagination->len;

	return FALSE;
}

/*
 * Computes the pages that will be output by a specific
 * print request.
 */
static void
compute_pages (G_GNUC_UNUSED GtkPrintOperation *operation,
	       PrintingInstance * pi)
{
	Workbook *wb = pi->wb;
	guint i;
	guint n;
	guint ct;
	PrintRange pr = pi->pr;
	guint from = pi->from;
	guint to = pi->to;

	switch (pr) {
	case GNM_PRINT_SAVED_INFO:
		/* This should never happen. */
	case GNM_PRINT_ACTIVE_SHEET:
		compute_sheet_pages_add_sheet (pi, pi->sheet, FALSE, FALSE);
		break;
	case GNM_PRINT_ALL_SHEETS:
		n = workbook_sheet_count (wb);
		for (i = 0; i < n; i++) {
			Sheet *sheet = workbook_sheet_by_index (wb, i);
			if (sheet->print_info->do_not_print)
				continue;
			if (!sheet_is_visible(sheet))
				continue;
			compute_sheet_pages_add_sheet (pi, sheet,
						       FALSE, FALSE);
		}
		break;
	case GNM_PRINT_ALL_SHEETS_INCLUDING_HIDDEN:
		n = workbook_sheet_count (wb);
		for (i = 0; i < n; i++) {
			Sheet *sheet = workbook_sheet_by_index (wb, i);
			if (sheet->print_info->do_not_print)
				continue;
			compute_sheet_pages_add_sheet (pi, sheet,
						       FALSE, FALSE);
		}
		break;
	case GNM_PRINT_SHEET_RANGE:
		if (from > to)
			break;
		n = workbook_sheet_count (wb);
		ct = 0;
		for (i = 0; i < n; i++){
			Sheet *sheet = workbook_sheet_by_index (wb, i);
			if (sheet_is_visible(sheet))
				ct++;
			else
				continue;
			if (sheet->print_info->do_not_print)
				continue;
			if ((ct >= from) && (ct <= to))
				compute_sheet_pages_add_sheet (pi, sheet,
							       FALSE, FALSE);
		}
		break;
	case GNM_PRINT_SHEET_SELECTION:
		compute_sheet_pages_add_sheet (pi, pi->sheet, TRUE, FALSE);
		break;
	case GNM_PRINT_IGNORE_PRINTAREA:
		compute_sheet_pages_add_sheet (pi, pi->sheet, FALSE, TRUE);
		break;
	case GNM_PRINT_SHEET_SELECTION_IGNORE_PRINTAREA:
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
	pj->render_info = gnm_print_hf_render_info_new ();
	pj->render_info->sheet = sheet;
	pj->render_info->page = 1;

	return pj;
}

static void
print_job_info_destroy (PrintJobInfo *pj)
{
	g_object_unref (pj->gp_config);
	gnm_print_hf_render_info_destroy (pj->render_info);
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

	if (gnm_debug_flag ("print"))
		g_printerr ("paginate %d\n", paginate);

	spi = g_list_nth_data (pi->gnmSheets, paginate);
	if (spi == NULL) { /*We are done paginating */
		/* GTK sends additional pagination requests! */
		/* We only need to do this once though! */
		if (g_list_nth_data (pi->gnmSheets, paginate - 1) != NULL) {
			GList *l;
			gint n_pages = 0;

			for (l = pi->gnmSheets; l != NULL; l = l->next) {
				SheetPrintInfo *spi = l->data;
				n_pages += spi->pages;
			}

			if (pi->preview && n_pages > 1000) {
				int i, count = 0;

				gtk_print_operation_set_n_pages
					(operation, n_pages == 0 ? 1 : n_pages);
				for (i = 0; i < n_pages; i++) {
					if (gtk_print_operation_preview_is_selected
					    (GTK_PRINT_OPERATION_PREVIEW (operation),
					     i))
						count++;
					if (count > 1000)
						break;
				}
				if (count > 1000 && !go_gtk_query_yes_no
				    (pi->progress != NULL ?
				     GTK_WINDOW (pi->progress) : wbcg_toplevel (WBC_GTK (pi->wbc)),
				     FALSE, "%s",
				     _("You have chosen more than 1000 pages to preview. "
				       "This may take a long time. "
				       "Do you really want to proceed?")))
					n_pages = 0;
			}

			gtk_print_operation_set_n_pages (operation, n_pages == 0 ? 1 : n_pages);
			gtk_print_operation_set_unit (operation, GTK_UNIT_POINTS);
			pi->hfi->pages = n_pages;

			if (n_pages == 0) /* gtk+ cannot handle 0 pages */
				gtk_print_operation_cancel (operation);
		}
		return TRUE;
	}

	if (compute_sheet_pages (context, pi, spi)) {
		gtk_print_operation_cancel (operation);
		return TRUE;
	}

	return FALSE;
}

static void
cb_progress_response (G_GNUC_UNUSED GtkDialog *dialog,
		      G_GNUC_UNUSED gint       response_id,
		      PrintingInstance *pi)
{
	pi->cancel = TRUE;
}

static gboolean
cb_progress_delete (G_GNUC_UNUSED GtkWidget *widget,
		    G_GNUC_UNUSED GdkEvent  *event,
		    PrintingInstance *pi)
{
	pi->cancel = TRUE;
	return TRUE;
}

static gboolean
gnm_ready_preview_cb (G_GNUC_UNUSED GtkPrintOperation *operation,
		      G_GNUC_UNUSED GtkPrintOperationPreview *preview,
		      G_GNUC_UNUSED GtkPrintContext *context,
		      G_GNUC_UNUSED GtkWindow *parent,
		      gpointer user_data)
{
	PrintingInstance * pi = (PrintingInstance *) user_data;
	pi->preview = TRUE;

	return FALSE;
}

static void
gnm_begin_print_cb (GtkPrintOperation *operation,
                    G_GNUC_UNUSED GtkPrintContext   *context,
		    gpointer           user_data)
{
	PrintingInstance * pi = (PrintingInstance *) user_data;

	if (gnm_debug_flag ("print"))
		g_printerr ("begin-print\n");

	{
		/* Working around gtk+ bug 423484. */
		GtkPrintSettings *settings = gtk_print_operation_get_print_settings (operation);
		gtk_print_settings_set_int
			(settings, GNUMERIC_PRINT_SETTING_PRINT_FROM_SHEET_KEY,
			 pi->from);
		gtk_print_settings_set_int
			(settings, GNUMERIC_PRINT_SETTING_PRINT_TO_SHEET_KEY,
			 pi->to);
		gtk_print_settings_set_int
			(settings, GNUMERIC_PRINT_SETTING_PRINTRANGE_KEY, pi->pr);
		gtk_print_settings_set_int
			(settings, GNUMERIC_PRINT_SETTING_IGNORE_PAGE_BREAKS_KEY, pi->ignore_pb ? 1 : 0);
	}

	if (NULL != pi->wbc && GNM_IS_WBC_GTK(pi->wbc)) {
		pi->progress = gtk_message_dialog_new (wbcg_toplevel (WBC_GTK (pi->wbc)),
						       GTK_DIALOG_MODAL |
						       GTK_DIALOG_DESTROY_WITH_PARENT,
						       GTK_MESSAGE_INFO,
						       GTK_BUTTONS_CANCEL,
						       "%s", /* please clang */
						       pi->preview ?
						       _("Preparing to preview"):
						       _("Preparing to print"));
		g_signal_connect (G_OBJECT (pi->progress), "response",
				  G_CALLBACK (cb_progress_response), pi);
		g_signal_connect (G_OBJECT (pi->progress), "delete-event",
				  G_CALLBACK (cb_progress_delete), pi);
		gtk_widget_show_all (pi->progress);
	}

	compute_pages (operation, pi);
}

static void
gnm_end_print_cb (G_GNUC_UNUSED GtkPrintOperation *operation,
                  G_GNUC_UNUSED GtkPrintContext   *context,
                  G_GNUC_UNUSED gpointer           user_data)
{
	if (gnm_debug_flag ("print"))
		g_printerr ("end-print\n");
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
                           G_GNUC_UNUSED GtkPrintContext   *context,
			   gint               page_nr,
			   GtkPageSetup      *setup,
			   gpointer           user_data)
{
	PrintingInstance * pi = (PrintingInstance *) user_data;
	Sheet *sheet;
	GtkPrintSettings* settings = gtk_print_operation_get_print_settings
				     (operation);

	g_return_if_fail (pi != NULL);

	sheet = print_get_sheet (pi, page_nr);

	if (sheet == NULL) {
		/* g_warning ("Avoiding gtk+ bug 492498"); */
		return;
	}

	gtk_print_settings_set_use_color (settings, !sheet->print_info->print_black_and_white);
	if (sheet->print_info->page_setup == NULL)
		gnm_print_info_load_defaults (sheet->print_info);
	if (sheet->print_info->page_setup != NULL)
		cp_gtk_page_setup (sheet->print_info->page_setup, setup);
}

static void
gnm_draw_page_cb (GtkPrintOperation *operation,
                  GtkPrintContext   *context,
		  gint               page_nr,
		  gpointer           user_data)
{

	PrintingInstance * pi = (PrintingInstance *) user_data;
	SheetPageRange * gsr;

	if (gnm_debug_flag ("print"))
		g_printerr ("draw-page %d\n", page_nr);

	if (pi->cancel) {
		gtk_print_operation_cancel (operation);
		g_signal_handlers_disconnect_by_func
			(G_OBJECT (operation), G_CALLBACK (gnm_draw_page_cb), user_data);
		return;
	}

	gsr = print_get_sheet_page_range (pi, page_nr);
	if (gsr) {
		if (pi->progress) {
			char *text;

			if (pi->hfi->pages == -1)
				text = g_strdup_printf
					(pi->preview ? _("Creating preview of page %3d")
					 : _("Printing page %3d"), page_nr);
			else
				text = g_strdup_printf
					(pi->preview ?
					 ngettext("Creating preview of page %3d of %3d page",
					          "Creating preview of page %3d of %3d pages",
					          pi->hfi->pages)
					 : ngettext("Printing page %3d of %3d page",
					            "Printing page %3d of %3d pages",
					            pi->hfi->pages),
					 page_nr, pi->hfi->pages);
			g_object_set (G_OBJECT (pi->progress), "text", text, NULL);
			g_free (text);
		}
		pi->hfi->page = page_nr + 1;
		pi->hfi->sheet = gsr->sheet;
		pi->hfi->page_area = gsr->range;
		pi->hfi->top_repeating = gsr->range.start;
		if (gsr->n_rep_cols > 0)
			pi->hfi->top_repeating.col = gsr->first_rep_cols;
		if (gsr->n_rep_rows > 0)
			pi->hfi->top_repeating.row = gsr->first_rep_rows;
		print_page (operation, context, pi, gsr);
		g_free (gsr);
	}
}

static void
widget_button_cb (GtkToggleButton *togglebutton, GtkWidget *check)
{
	gtk_widget_set_sensitive (check, gtk_toggle_button_get_active (togglebutton));
}

static guint
workbook_visible_sheet_count (Workbook *wb)
{
	guint i;
	guint n = workbook_sheet_count (wb);
	guint count = 0;

	for (i = 0; i < n; i++) {
		Sheet *sheet = workbook_sheet_by_index (wb, i);
		if (sheet_is_visible(sheet))
			count++;
	}
	return count;
}

static GObject*
gnm_create_widget_cb (GtkPrintOperation *operation, gpointer user_data)
{
	PrintingInstance * pi = (PrintingInstance *) user_data;
	GtkWidget *grid;
	GtkWidget *button_all_sheets, *button_selected_sheet, *button_spec_sheets;
	GtkWidget *button_selection, *button_ignore_printarea;
	GtkWidget *button_print_hidden_sheets;
	GtkWidget *label_from, *label_to;
	GtkWidget *spin_from, *spin_to;
	GtkWidget *button_ignore_page_breaks;
	GtkPrintSettings * settings;
	guint n_sheets = workbook_visible_sheet_count (pi->wb);

	if (gnm_debug_flag ("print"))
		g_printerr ("Creating custom print widget\n");

	grid = gtk_grid_new ();
	g_object_set (grid,
	              "column-spacing", 12,
	              "row-spacing", 6,
	              "border-width", 6,
	              NULL);

	button_all_sheets = gtk_radio_button_new_with_mnemonic (NULL,
								_("_All workbook sheets"));
	gtk_widget_set_hexpand (button_all_sheets, TRUE);
	gtk_grid_attach (GTK_GRID (grid), button_all_sheets, 0, 0, 5, 1);

	button_print_hidden_sheets  = gtk_check_button_new_with_mnemonic
		(_("Also print _hidden sheets"));
	g_object_set (button_print_hidden_sheets,
	             "hexpand", TRUE,
	             "margin-left", 24,
	             NULL);
	gtk_grid_attach (GTK_GRID (grid), button_print_hidden_sheets, 0, 1, 5, 1);

	button_selected_sheet = gtk_radio_button_new_with_mnemonic_from_widget
		(GTK_RADIO_BUTTON (button_all_sheets), _("A_ctive workbook sheet"));
	gtk_widget_set_hexpand (button_selected_sheet, TRUE);
	gtk_grid_attach (GTK_GRID (grid), button_selected_sheet, 0, 2, 5, 1);

	button_spec_sheets = gtk_radio_button_new_with_mnemonic_from_widget
		(GTK_RADIO_BUTTON (button_all_sheets), _("_Workbook sheets:"));
	gtk_widget_set_hexpand (button_spec_sheets, TRUE);
	gtk_grid_attach (GTK_GRID (grid), button_spec_sheets, 0, 5, 1, 1);

	button_selection = gtk_check_button_new_with_mnemonic
		(_("Current _selection only"));
	g_object_set (button_selection,
	             "hexpand", TRUE,
	             "margin-left", 24,
	             NULL);
	gtk_grid_attach (GTK_GRID (grid), button_selection, 0, 3, 5, 1);

	button_ignore_printarea  = gtk_check_button_new_with_mnemonic
		(_("_Ignore defined print area"));
	g_object_set (button_ignore_printarea,
	             "hexpand", TRUE,
	             "margin-left", 24,
	             NULL);
	gtk_grid_attach (GTK_GRID (grid), button_ignore_printarea, 0, 4, 5, 1);

	label_from = gtk_label_new (_("from:"));
	g_object_set (label_from,
	             "hexpand", TRUE,
	             "margin-left", 24,
	             NULL);
	gtk_grid_attach (GTK_GRID (grid), label_from, 1, 5, 1, 1);

	spin_from = gtk_spin_button_new_with_range (1, n_sheets, 1);
	gtk_widget_set_hexpand (spin_from, TRUE);
	gtk_grid_attach (GTK_GRID (grid), spin_from, 2, 5, 1, 1);

	label_to = gtk_label_new (_("to:"));
	gtk_widget_set_hexpand (label_to, TRUE);
	gtk_grid_attach (GTK_GRID (grid), label_to, 3, 5, 1, 1);

	spin_to = gtk_spin_button_new_with_range (1, n_sheets, 1);
	gtk_widget_set_hexpand (spin_to, TRUE);
	gtk_grid_attach (GTK_GRID (grid), spin_to, 4, 5, 1, 1);
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin_to), n_sheets);

	button_ignore_page_breaks = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
	gtk_widget_set_hexpand (button_ignore_page_breaks, TRUE);
	gtk_grid_attach (GTK_GRID (grid), button_ignore_page_breaks, 0, 6, 5, 1);

	button_ignore_page_breaks = gtk_check_button_new_with_mnemonic (_("Ignore all _manual page breaks"));
	gtk_widget_set_hexpand (button_ignore_page_breaks, TRUE);
	gtk_grid_attach (GTK_GRID (grid), button_ignore_page_breaks, 0, 7, 5, 1);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button_ignore_page_breaks), TRUE);

	g_signal_connect_after (G_OBJECT (button_selected_sheet), "toggled",
		G_CALLBACK (widget_button_cb), button_selection);
	g_signal_connect_after (G_OBJECT (button_selected_sheet), "toggled",
		G_CALLBACK (widget_button_cb), button_ignore_printarea);

	g_signal_connect_after (G_OBJECT (button_all_sheets), "toggled",
		G_CALLBACK (widget_button_cb), button_print_hidden_sheets);

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
			 GNM_PRINT_ACTIVE_SHEET)) {
		case GNM_PRINT_SHEET_SELECTION_IGNORE_PRINTAREA:
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button_ignore_printarea), TRUE);
			/* no break */
		case GNM_PRINT_SHEET_SELECTION:
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button_selection), TRUE);
			/* no break */
		case GNM_PRINT_ACTIVE_SHEET:
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button_selected_sheet), TRUE);
			break;
		case GNM_PRINT_IGNORE_PRINTAREA:
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button_ignore_printarea), TRUE);
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button_selected_sheet), TRUE);
			break;
		case GNM_PRINT_SHEET_RANGE:
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button_spec_sheets), TRUE);
			break;
		case GNM_PRINT_ALL_SHEETS_INCLUDING_HIDDEN:
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button_print_hidden_sheets), TRUE);
			/* no break */
		case GNM_PRINT_ALL_SHEETS:
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
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button_ignore_page_breaks),
					      0 != gtk_print_settings_get_int_with_default
					      (settings, GNUMERIC_PRINT_SETTING_IGNORE_PAGE_BREAKS_KEY,
					       0));
	}

	/* We are sending toggled signals to ensure that all widgets are */
	/* correctly enabled or disabled.                                */
	gtk_toggle_button_toggled (GTK_TOGGLE_BUTTON (button_selected_sheet));
	gtk_toggle_button_toggled (GTK_TOGGLE_BUTTON (button_spec_sheets));

	gtk_widget_show_all (grid);

	/* Let's save the widgets */
	pi->button_all_sheets = button_all_sheets;
	pi->button_selected_sheet = button_selected_sheet;
	pi->button_spec_sheets = button_spec_sheets;
	pi->button_selection = button_selection;
	pi->button_ignore_printarea = button_ignore_printarea;
	pi->button_print_hidden_sheets = button_print_hidden_sheets;
	pi->spin_from = spin_from;
	pi->spin_to = spin_to;
	pi->button_ignore_page_breaks = button_ignore_page_breaks;

	if (gnm_debug_flag ("print"))
		g_printerr ("Done with creating custom print widget\n");

	return G_OBJECT (grid);
}

static void
gnm_custom_widget_apply_cb (GtkPrintOperation       *operation,
			    G_GNUC_UNUSED GtkWidget *widget,
			    gpointer                 user_data)
{
	PrintingInstance * pi = (PrintingInstance *) user_data;
	GtkPrintSettings * settings;
	PrintRange pr = GNM_PRINT_ACTIVE_SHEET;
	guint from, to;
	gboolean ignore_pb;

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
		if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (pi->button_print_hidden_sheets)))
			pr = GNM_PRINT_ALL_SHEETS_INCLUDING_HIDDEN;
		else
			pr = GNM_PRINT_ALL_SHEETS;
	} else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (pi->button_spec_sheets))) {
		pr = GNM_PRINT_SHEET_RANGE;
	} else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (pi->button_selected_sheet))) {
		gboolean ignore_printarea = gtk_toggle_button_get_active
			(GTK_TOGGLE_BUTTON (pi->button_ignore_printarea));
		gboolean selection = gtk_toggle_button_get_active
			(GTK_TOGGLE_BUTTON (pi->button_selection));
		if (selection && ignore_printarea)
			pr = GNM_PRINT_SHEET_SELECTION_IGNORE_PRINTAREA;
		else if (selection)
			pr = GNM_PRINT_SHEET_SELECTION;
		else if (ignore_printarea)
			pr = GNM_PRINT_IGNORE_PRINTAREA;
		else
			pr = GNM_PRINT_ACTIVE_SHEET;
	}

	gtk_print_settings_set_int (settings,
				    GNUMERIC_PRINT_SETTING_PRINTRANGE_KEY, pr);

	pi->pr = pr;

	ignore_pb= gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (pi->button_ignore_page_breaks)) ? 1 : 0;
	gtk_print_settings_set_int (settings, GNUMERIC_PRINT_SETTING_IGNORE_PAGE_BREAKS_KEY,
				    ignore_pb);
	pi->ignore_pb = ignore_pb;
}

static void
cb_delete_and_free (char *tmp_file_name)
{
	if (tmp_file_name) {
		g_unlink (tmp_file_name);
		g_free (tmp_file_name);
	}
}

static gchar *
gnm_print_uri_change_extension (char const *uri, GtkPrintSettings* settings)
{
	const gchar *ext = gtk_print_settings_get
		(settings,
		 GTK_PRINT_SETTINGS_OUTPUT_FILE_FORMAT);
	gchar *base;
	gchar *used_ext;
	gint strip;
	gchar *res;
	gint uri_len = strlen(uri);

	if (ext == NULL) {
		ext = "pdf";
		gtk_print_settings_set (settings,
					GTK_PRINT_SETTINGS_OUTPUT_FILE_FORMAT,
					ext);
	}

	base     = g_path_get_basename (uri);
	used_ext = strrchr (base, '.');
	if (used_ext == NULL)
		return g_strconcat (uri, ".", ext, NULL);
	strip = strlen (base) - (used_ext - base);
	res = g_strndup (uri, uri_len - strip + 1 + strlen (ext));
	res[uri_len - strip] = '.';
	strcpy (res + uri_len - strip + 1, ext);
	return res;
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
	GtkWindow *parent = NULL;
	GtkPrintOperationAction action;
	gchar *tmp_file_name = NULL;
	int tmp_file_fd = -1;
	gboolean preview_via_pdf = FALSE;
	static const PrintRange pr_translator[] = {
		GNM_PRINT_ACTIVE_SHEET, GNM_PRINT_ALL_SHEETS,
		GNM_PRINT_ALL_SHEETS, GNM_PRINT_ACTIVE_SHEET,
		GNM_PRINT_SHEET_SELECTION, GNM_PRINT_ACTIVE_SHEET,
		GNM_PRINT_SHEET_SELECTION_IGNORE_PRINTAREA
	};
	GODoc *doc;
	gchar *output_uri = NULL;
	gchar const *saved_uri = NULL;

#ifdef PREVIEW_VIA_PDF
	preview_via_pdf = preview;
#endif

	g_return_if_fail (sheet != NULL && sheet->workbook != NULL);

	if (preview)
		g_return_if_fail (!export_dst && wbc);

	doc = GO_DOC (sheet->workbook);

	print = gtk_print_operation_new ();

	pi = printing_instance_new ();
	pi->wb = sheet->workbook;
	pi->wbc = wbc ? GNM_WBC (wbc) : NULL;
	pi->sheet = sheet;
	pi->preview = preview;

	settings = gnm_conf_get_print_settings ();
	if (default_range == GNM_PRINT_SAVED_INFO) {
		gint dr = print_info_get_printrange (sheet->print_info);
		if (dr < 0 || dr >= (gint)G_N_ELEMENTS (pr_translator))
			default_range = GNM_PRINT_ACTIVE_SHEET;
		else
			default_range = pr_translator[dr];
	}
	gtk_print_settings_set_int (settings,
				    GNUMERIC_PRINT_SETTING_PRINTRANGE_KEY,
				    default_range);
	pi->pr = default_range;
	gtk_print_settings_set_use_color (settings,
					  !sheet->print_info->print_black_and_white);
	if (!export_dst && !preview_via_pdf && !preview) {
		/* We should be setting the output file name to something */
		/* reasonable */
		saved_uri = print_info_get_printtofile_uri (sheet->print_info);
		if (saved_uri != NULL &&
		    g_ascii_strncasecmp (doc->uri, "file:///", 8) == 0)
			output_uri = gnm_print_uri_change_extension (saved_uri,
								     settings);
		else
			saved_uri = NULL;
		if (output_uri == NULL && doc->uri != NULL
		    && g_ascii_strncasecmp (doc->uri, "file:///", 8) == 0)
			output_uri = gnm_print_uri_change_extension (doc->uri,
								     settings);
		if (output_uri != NULL) {
			gtk_print_settings_set (settings,
						GTK_PRINT_SETTINGS_OUTPUT_URI,
						output_uri);
			g_free (output_uri);
		}
	}

	gtk_print_operation_set_print_settings (print, settings);
	g_object_unref (settings);

	page_setup = gnm_print_info_get_page_setup (sheet->print_info);
	if (page_setup)
		gtk_print_operation_set_default_page_setup (print, page_setup);

	g_signal_connect (print, "preview", G_CALLBACK (gnm_ready_preview_cb), pi);
	g_signal_connect (print, "begin-print", G_CALLBACK (gnm_begin_print_cb), pi);
	g_signal_connect (print, "paginate", G_CALLBACK (gnm_paginate_cb), pi);
	g_signal_connect (print, "draw-page", G_CALLBACK (gnm_draw_page_cb), pi);
	g_signal_connect (print, "end-print", G_CALLBACK (gnm_end_print_cb), pi);
	g_signal_connect (print, "request-page-setup", G_CALLBACK (gnm_request_page_setup_cb), pi);

	gtk_print_operation_set_use_full_page (print, FALSE);
	gtk_print_operation_set_unit (print, GTK_UNIT_POINTS);

	if (NULL != wbc && GNM_IS_WBC_GTK(wbc))
		parent = wbcg_toplevel (WBC_GTK (wbc));

	if (preview_via_pdf || export_dst) {
		GError *err = NULL;

		tmp_file_fd = g_file_open_tmp ("gnmXXXXXX.pdf",
					       &tmp_file_name, &err);
		if (err) {
			if (export_dst)
				gsf_output_set_error (export_dst, 0,
						      "%s", err->message);
			else {
				char *text = g_strdup_printf
					(_("Failed to create temporary file for printing: %s"),
					 err->message);
				go_cmd_context_error_export
					(GO_CMD_CONTEXT (wbc), text);
				g_free (text);
			}
			g_error_free (err);
			goto out;
		}

		action = GTK_PRINT_OPERATION_ACTION_EXPORT;
		gtk_print_operation_set_export_filename (print, tmp_file_name);
		gtk_print_operation_set_show_progress (print, FALSE);
	} else {
		action = preview
			? GTK_PRINT_OPERATION_ACTION_PREVIEW
			: GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG;
		gtk_print_operation_set_show_progress (print, FALSE);
		gtk_print_operation_set_custom_tab_label (print, _("Gnumeric Print Range"));
		g_signal_connect (print, "create-custom-widget", G_CALLBACK (gnm_create_widget_cb), pi);
		g_signal_connect (print, "custom-widget-apply", G_CALLBACK (gnm_custom_widget_apply_cb), pi);
	}

	res = gtk_print_operation_run (print, action, parent, NULL);

	switch (res) {
	case GTK_PRINT_OPERATION_RESULT_APPLY:
		if (action == GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG) {
			char const *printer;
			settings = gtk_print_operation_get_print_settings (print);
			gnm_conf_set_print_settings (settings);
			gnm_insert_meta_date (doc, GSF_META_NAME_PRINT_DATE);
			printer = gtk_print_settings_get_printer (settings);
			if (strcmp (printer, "Print to File") == 0 ||
			    strcmp (printer, _("Print to File")) == 0) {
				gchar *wb_output_uri =
					gnm_print_uri_change_extension (doc->uri,
									settings);
				print_info_set_printtofile_from_settings
					(sheet->print_info, settings, wb_output_uri);
				g_free (wb_output_uri);
			}
		}
		print_info_set_from_settings
			(sheet->print_info, settings);
		break;
	case GTK_PRINT_OPERATION_RESULT_CANCEL:
		break;
	case GTK_PRINT_OPERATION_RESULT_ERROR:
		break;
	case GTK_PRINT_OPERATION_RESULT_IN_PROGRESS:
		/* This can only happen if we were allowing asynchronous operation */
		break;
	default: ;
	}
	printing_instance_delete (pi);

	if (preview_via_pdf) {
#ifdef G_OS_WIN32
		/* For some reason the general code doesn't work for me.
		   Be brutal, even if this might not work for non-ASCII
		   filenames.  */
		int res = (int)ShellExecute (NULL, "open",
					     tmp_file_name,
					     NULL,
					     NULL,
					     SW_SHOW);
		if (gnm_debug_flag ("preview")) {
			g_printerr ("tmp_file_name=%s\n", tmp_file_name);
			g_printerr ("res=%d\n", res);
		}
#else
		GdkScreen *screen = parent
			? gtk_widget_get_screen (GTK_WIDGET (parent))
			: NULL;
		char *url = go_filename_to_uri (tmp_file_name);
		go_gtk_url_show (url, screen);
		g_free (url);
#endif

		/* We hook this up to delete the temp file when the workbook
		   is closed or when a new preview is done for the same
		   workbook.  That's not perfect, but good enough while
		   we wait for gtk+ to fix printing.  */
		g_object_set_data_full (G_OBJECT (wbc),
					"temp-file", tmp_file_name,
					(GDestroyNotify)cb_delete_and_free);
		tmp_file_name = NULL;
	} else if (tmp_file_name) {
		char buffer[64 * 1024];
		gssize bytes_read;

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
						      "%s", g_strerror (save_errno));
		}
	}

 out:
	if (tmp_file_fd >= 0)
		close (tmp_file_fd);
	cb_delete_and_free (tmp_file_name);

	g_object_unref (print);
}

static void
gnm_draw_so_page_cb (G_GNUC_UNUSED GtkPrintOperation *operation,
		     GtkPrintContext                 *context,
		     G_GNUC_UNUSED gint               page_nr,
		     gpointer                         user_data)
{

	SheetObject *so = (SheetObject *) user_data;
	cairo_t *cr= gtk_print_context_get_cairo_context (context);
	Sheet *sheet = sheet_object_get_sheet (so);

	cairo_save (cr);
	cairo_translate (cr, 0, 0);
	sheet_object_draw_cairo (so, (gpointer)cr, sheet->text_is_rtl);
	cairo_restore (cr);
}

/**
 * gnm_print_so:
 * @wbc:
 * @sos: (element-type SheetObject) (transfer none):
 * @export_dst:
 */
void
gnm_print_so (WorkbookControl *wbc, GPtrArray *sos,
	      GsfOutput *export_dst)
{
	GtkPrintOperation *print;
	GtkPageSetup *page_setup;
	GtkPrintSettings* settings;
	Sheet *sheet;
	GtkWindow *parent = NULL;
	GtkPrintOperationAction action;
	gchar *tmp_file_name = NULL;
	int tmp_file_fd = -1;
	SheetObject *so;

	g_return_if_fail (sos != NULL && sos->len > 0);

	/* FIXME: we should print all objects in the array, not just the first! */

	so = g_ptr_array_index (sos, 0),
	sheet = sheet_object_get_sheet (so);
	if (NULL != wbc && GNM_IS_WBC_GTK(wbc))
		parent = wbcg_toplevel (WBC_GTK (wbc));

	print = gtk_print_operation_new ();

	settings = gnm_conf_get_print_settings ();
	gtk_print_settings_set_use_color (settings,
					  !sheet->print_info->print_black_and_white);
	gtk_print_operation_set_print_settings (print, settings);
	g_object_unref (settings);

	page_setup = gnm_print_info_get_page_setup (sheet->print_info);
	if (page_setup)
		gtk_print_operation_set_default_page_setup (print, page_setup);

	gtk_print_operation_set_n_pages (print, 1);
	gtk_print_operation_set_embed_page_setup (print, TRUE);

	g_signal_connect (print, "draw-page", G_CALLBACK (gnm_draw_so_page_cb), so);

	gtk_print_operation_set_use_full_page (print, FALSE);
	gtk_print_operation_set_unit (print, GTK_UNIT_POINTS);

	if (export_dst) {
		GError *err = NULL;

		tmp_file_fd = g_file_open_tmp ("gnmXXXXXX.pdf",
					       &tmp_file_name, &err);
		if (err) {
			gsf_output_set_error (export_dst, 0,
					      "%s", err->message);
			g_error_free (err);
			if (tmp_file_fd >= 0)
				close (tmp_file_fd);
			cb_delete_and_free (tmp_file_name);

			g_object_unref (print);
			return;
		}
		action = GTK_PRINT_OPERATION_ACTION_EXPORT;
		gtk_print_operation_set_export_filename (print, tmp_file_name);
		gtk_print_operation_set_show_progress (print, FALSE);
	} else {
		action = GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG;
		gtk_print_operation_set_show_progress (print, TRUE);
	}

	gtk_print_operation_run (print, action, parent, NULL);

	if (tmp_file_name) {
		char buffer[64 * 1024];
		gssize bytes_read;

		if (lseek (tmp_file_fd, 0, SEEK_SET) < 0)
			bytes_read = -1;
		else {
			while ((bytes_read = read
				(tmp_file_fd, buffer, sizeof (buffer))) > 0) {
				gsf_output_write (export_dst, bytes_read, buffer);
			}
		}
		if (bytes_read < 0) {
			int save_errno = errno;
			if (!gsf_output_error (export_dst))
				gsf_output_set_error (export_dst,
						      g_file_error_from_errno (save_errno),
						      "%s", g_strerror (save_errno));
		}
		close (tmp_file_fd);
		cb_delete_and_free (tmp_file_name);
	}

	g_object_unref (print);
}

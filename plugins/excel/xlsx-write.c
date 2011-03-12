/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * xlsx-write.c : export MS Office Open xlsx files.
 *
 * Copyright (C) 2006-2007 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */

/*****************************************************************************/

#include <gnumeric-config.h>
#include "ms-excel-write.h"
#include "xlsx-utils.h"

#include "parse-util.h"
#include "workbook.h"
#include "workbook-priv.h"
#include "workbook-view.h"
#include "sheet.h"
#include "sheet-style.h"
#include "sheet-view.h"
#include "sheet-filter.h"
#include "ranges.h"
#include "value.h"
#include "cell.h"
#include "expr.h"
#include "expr-impl.h"
#include "func.h"
#include "style-color.h"
#include "validation.h"
#include "hlink.h"
#include "input-msg.h"
#include "print-info.h"
#include "gutils.h"
#include "sheet-object.h"
#include "sheet-object-cell-comment.h"
#include "sheet-object-graph.h"
#include "graph.h"

#include "go-val.h"

#include <goffice/goffice.h>

#include <gsf/gsf-output.h>
#include <gsf/gsf-outfile.h>
#include <gsf/gsf-outfile-zip.h>
#include <gsf/gsf-open-pkg-utils.h>
#include <gsf/gsf-utils.h>
#include <gsf/gsf-libxml.h>
#include <glib/gi18n-lib.h>
#include <gmodule.h>
#include <string.h>

static char const *ns_ss	 = "http://schemas.openxmlformats.org/spreadsheetml/2006/main";
static char const *ns_ss_drawing = "http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing";
static char const *ns_drawing	 = "http://schemas.openxmlformats.org/drawingml/2006/main";
static char const *ns_chart	 = "http://schemas.openxmlformats.org/drawingml/2006/chart";
static char const *ns_rel	 = "http://schemas.openxmlformats.org/officeDocument/2006/relationships";
static char const *ns_rel_hlink	 = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink";
static char const *ns_rel_draw	 = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing";
static char const *ns_rel_chart	 = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart";
static char const *ns_rel_com	 = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/comments";

typedef struct {
	XLExportBase base;

	Sheet const	*sheet;
	GHashTable	*shared_string_hash;
	GPtrArray	*shared_string_array;
	GnmConventions	*convs;
	GOIOContext	*io_context;

	GsfOutfile	*xl_dir;
	struct {
		unsigned int	count;
		GsfOutfile	*dir;
	} chart, drawing, pivotCache, pivotTable;
	unsigned comment;
	GOFormat *date_fmt;
} XLSXWriteState;

typedef struct {
	XLSXWriteState	*state;
	GsfXMLOut	*xml;
} XLSXClosure;

static void
xlsx_add_bool (GsfXMLOut *xml, char const *id, gboolean val)
{
	gsf_xml_out_add_cstr_unchecked (xml, id, val ? "1" : "0");
}
static void
xlsx_add_rgb (GsfXMLOut *xml, char const *id, GOColor c)
{
	char buf [3 * 4 * sizeof (unsigned int) + 1];
	sprintf (buf, "%02X%02X%02X%02X",
		 GO_COLOR_UINT_A (c), GO_COLOR_UINT_R (c),
		 GO_COLOR_UINT_G (c), GO_COLOR_UINT_B (c));
	gsf_xml_out_add_cstr_unchecked (xml, id, buf);
}
static void
xlsx_add_pos (GsfXMLOut *xml, char const *id, GnmCellPos const *pos)
{
	gsf_xml_out_add_cstr_unchecked (xml, id,
		cellpos_as_string (pos));
}
static void
xlsx_add_range (GsfXMLOut *xml, char const *id, GnmRange const *range)
{
	gsf_xml_out_add_cstr_unchecked (xml, id,
		range_as_string (range));
}
static void
xlsx_add_range_list (GsfXMLOut *xml, char const *id, GSList const *ranges)
{
	GString *accum = g_string_new (NULL);

	for (; NULL != ranges ; ranges = ranges->next) {
		g_string_append (accum, range_as_string (ranges->data));
		if (NULL != ranges->next)
			g_string_append_c (accum, ' ');
	}

	gsf_xml_out_add_cstr_unchecked (xml, id, accum->str);
	g_string_free (accum, TRUE);
}

/****************************************************************************/

static void
xlsx_write_shared_strings (XLSXWriteState *state, GsfOutfile *wb_part)
{
	if (state->shared_string_array->len > 0) {
		unsigned i;
		GOString const *str;
		GsfOutput *part = gsf_outfile_open_pkg_add_rel (state->xl_dir, "sharedStrings.xml",
			"application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml",
			wb_part,
			"http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings");
		GsfXMLOut *xml = gsf_xml_out_new (part);

		gsf_xml_out_start_element (xml, "sst");
		gsf_xml_out_add_cstr_unchecked (xml, "xmlns", ns_ss);
		gsf_xml_out_add_cstr_unchecked (xml, "xml:space", "preserve");
		gsf_xml_out_add_int (xml, "uniqueCount", state->shared_string_array->len);
		gsf_xml_out_add_int (xml, "count", state->shared_string_array->len);

		for (i = 0 ; i < state->shared_string_array->len ; i++) {
			gsf_xml_out_start_element (xml, "si");
			gsf_xml_out_start_element (xml, "t");
			str = g_ptr_array_index (state->shared_string_array, i);
			gsf_xml_out_add_cstr (xml, NULL, str->str);
			gsf_xml_out_end_element (xml); /* </t> */
			gsf_xml_out_end_element (xml); /* </si> */
		}

		gsf_xml_out_end_element (xml); /* </sst> */

		g_object_unref (xml);
		gsf_output_close (part);
		g_object_unref (part);
	}
}

static void
xlsx_write_fonts (XLSXWriteState *state, GsfXMLOut *xml)
{
}
static void
xlsx_write_fills (XLSXWriteState *state, GsfXMLOut *xml)
{
}
static void
xlsx_write_borders (XLSXWriteState *state, GsfXMLOut *xml)
{
}

/*
cellStyleXfs
cellXfs
cellStyles
dxfs
tableStyles
colors
*/

static void
xlsx_write_styles (XLSXWriteState *state, GsfOutfile *wb_part)
{
	GsfOutput *part = gsf_outfile_open_pkg_add_rel (state->xl_dir, "styles.xml",
		"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml",
		wb_part,
		"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles");
	GsfXMLOut *xml = gsf_xml_out_new (part);

	gsf_xml_out_start_element (xml, "styleSheet");
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns", ns_ss);
	gsf_xml_out_add_cstr_unchecked (xml, "xml:space", "preserve");

	xlsx_write_fonts (state, xml);
	xlsx_write_fills (state, xml);
	xlsx_write_borders (state, xml);

	gsf_xml_out_end_element (xml); /* </styleSheet> */

	g_object_unref (xml);
	gsf_output_close (part);
	g_object_unref (part);
}

#define XLSX_MAX_COLS	gnm_sheet_get_max_cols (state->sheet)	/* default is (2^14) */
#define XLSX_MAX_ROWS	gnm_sheet_get_max_rows (state->sheet)	/* default is (2^20) */

static void
xlsx_write_sheet_view (GsfXMLOut *xml, SheetView const *sv)
{
	Sheet const *sheet = sv_sheet (sv);
	GnmColor *sheet_auto   = sheet_style_get_auto_pattern_color (sheet);
	GnmColor *default_auto = style_color_auto_pattern ();
	GnmCellPos topLeft, frozen_topLeft;
	char const *activePane = NULL;
	int const frozen_height = sv->unfrozen_top_left.row -
		sv->frozen_top_left.row;
	int const frozen_width = sv->unfrozen_top_left.col -
		sv->frozen_top_left.col;
	int tmp;

	if (frozen_width > 0) {
		topLeft.col		= sv->frozen_top_left.col;
		frozen_topLeft.col	= sv->initial_top_left.col;
	} else {
		topLeft.col		= sv->initial_top_left.col;
		frozen_topLeft.col	= sv->frozen_top_left.col;
	}
	if (frozen_height > 0) {
		topLeft.row		= sv->frozen_top_left.row;
		frozen_topLeft.row	= sv->initial_top_left.row;
	} else {
		topLeft.row		= sv->initial_top_left.row;
		frozen_topLeft.row	= sv->frozen_top_left.row;
	}

	gsf_xml_out_start_element (xml, "sheetView");
	if (topLeft.col > 0 || topLeft.row > 0) /* A1 is the default */
		xlsx_add_pos (xml, "topLeftCell", &topLeft);
	gsf_xml_out_add_int (xml, "workbookViewId",
		wb_view_get_index_in_wb (sv_wbv (sv)));

	tmp = (int) (100.* sheet->last_zoom_factor_used + .5);
	if (tmp != 100)
		gsf_xml_out_add_int (xml, "zoomScale", tmp);

	switch (sv->view_mode) {
	case GNM_SHEET_VIEW_NORMAL_MODE : break;
	case GNM_SHEET_VIEW_PAGE_BREAK_MODE :
		gsf_xml_out_add_cstr_unchecked (xml, "view", "pageBreakPreview"); break;
	case GNM_SHEET_VIEW_LAYOUT_MODE :
		gsf_xml_out_add_cstr_unchecked (xml, "view", "pageLayout"); break;
	}

	if (sheet->hide_grid)
		gsf_xml_out_add_cstr_unchecked (xml, "showGridLines", "0");
	if (sheet->display_formulas)
		gsf_xml_out_add_cstr_unchecked (xml, "showFormulas", "1");
	if (sheet->hide_col_header || sheet->hide_row_header)
		gsf_xml_out_add_cstr_unchecked (xml, "showRowColHeaders", "0");
	if (sheet->hide_zero)
		gsf_xml_out_add_cstr_unchecked (xml, "showZeros", "0");
	if (!sheet->display_outlines)
		gsf_xml_out_add_cstr_unchecked (xml, "showOutlineSymbols", "0");
	if (sheet->text_is_rtl)
		gsf_xml_out_add_cstr_unchecked (xml, "rightToLeft", "1");
	if (sheet == wb_view_cur_sheet (sv_wbv (sv)))
		gsf_xml_out_add_cstr_unchecked (xml, "tabSelected", "1");

	if (!style_color_equal (sheet_auto, default_auto)) {
		gsf_xml_out_add_cstr_unchecked (xml, "defaultGridColor", "1");
#if 0
		gsf_xml_out_add_int (xml, "colorId", grid_color_index);
#endif
	}
	style_color_unref (sheet_auto);
	style_color_unref (default_auto);

	if (sv_is_frozen (sv)) {
		activePane = "bottomRight"; /* h&v freeze */

		gsf_xml_out_start_element (xml, "pane");
		if (frozen_width > 0)
			gsf_xml_out_add_int (xml, "xSplit", frozen_width);
		else
			activePane = "bottomLeft"; /* v freeze */
		if (frozen_height > 0)
			gsf_xml_out_add_int (xml, "ySplit", frozen_height);
		else
			activePane = "topRight"; /* h freeze */
		xlsx_add_pos (xml, "topLeftCell", &frozen_topLeft);
		gsf_xml_out_add_cstr_unchecked (xml, "activePane", activePane);
		gsf_xml_out_add_cstr_unchecked (xml, "state", "frozen");
		gsf_xml_out_end_element (xml); /* </pane> */
	}

	gsf_xml_out_start_element (xml, "selection");
	if (NULL != activePane)
		gsf_xml_out_add_cstr_unchecked (xml, "pane", activePane);
	/* activeCellId is always 0 for gnumeric */
	xlsx_add_pos (xml, "activeCell", &sv->edit_pos);
	xlsx_add_range_list (xml, "sqref", sv->selections);
	gsf_xml_out_end_element (xml); /* </selection> */

	gsf_xml_out_end_element (xml); /* </sheetView> */
}

static void
xlsx_write_init_row (gboolean *needs_row, GsfXMLOut *xml, int r, char const *span)
{
	if (*needs_row) {
		gsf_xml_out_start_element (xml, "row");
		gsf_xml_out_add_int (xml, "r", r+1);
		gsf_xml_out_add_cstr_unchecked (xml, "spans", span);;
		*needs_row = FALSE;
	}
}

static void
xlsx_write_cells (XLSXWriteState *state, GsfXMLOut *xml, GnmRange const *extent)
{
	int r, c;
	char const *type;
	char *content;
	int   str_id = -1;
	GnmParsePos pp;
	GnmCell const *cell;
	GnmExprTop const *texpr;
	GnmExprArrayCorner const *array;
	ColRowInfo const *ri;
	GnmValue const *val;
	gpointer tmp;
	char *cheesy_span = g_strdup_printf ("%d:%d", extent->start.col+1, extent->end.col+1);

	gsf_xml_out_start_element (xml, "sheetData");
	for (r = extent->start.row ; r <= extent->end.row ; r++) {
		gboolean needs_row = TRUE;
		if (NULL != (ri = sheet_row_get (state->sheet, r))) {
			if (ri->hard_size) {
				xlsx_write_init_row (&needs_row, xml, r, cheesy_span);
				gsf_xml_out_add_float (xml, "ht", ri->size_pts, 4);
				gsf_xml_out_add_cstr_unchecked (xml, "customHeight", "1");
			}
			if (ri->is_collapsed) {
				xlsx_write_init_row (&needs_row, xml, r, cheesy_span);
				gsf_xml_out_add_cstr_unchecked (xml, "collapsed", "1");
			}
			if (!ri->visible) {
				xlsx_write_init_row (&needs_row, xml, r, cheesy_span);
				gsf_xml_out_add_cstr_unchecked (xml, "hidden", "1");
			}
			if (ri->outline_level > 0) {
				xlsx_write_init_row (&needs_row, xml, r, cheesy_span);
				gsf_xml_out_add_int (xml, "outlineLevel", ri->outline_level);
			}
		}

		for (c = extent->start.col ; c <= extent->end.col ; c++) {
			if (NULL != (cell = sheet_cell_get (state->sheet, c, r))) {
				xlsx_write_init_row (&needs_row, xml, r, cheesy_span);
				val = cell->value;
				gsf_xml_out_start_element (xml, "c");
				gsf_xml_out_add_cstr_unchecked (xml, "r",
					cell_coord_name (c, r));

				switch (val->type) {
				default :
				case VALUE_EMPTY :	type = NULL; break; /* FIXME : what to do ? */
				case VALUE_BOOLEAN :	type = "b"; break;
				case VALUE_FLOAT :	type = ""; break; /* "n" is the default */
				case VALUE_ERROR :	type = "e"; break;
				case VALUE_STRING :
					/* A reasonable approximation of * 'is_shared'.  It can get spoofed by
					 * rich text references to a base * string */
					if (go_string_get_ref_count (val->v_str.val) > 1) {
						if (NULL == (tmp = g_hash_table_lookup (state->shared_string_hash, val->v_str.val))) {
							tmp = GINT_TO_POINTER (state->shared_string_array->len);
							g_ptr_array_add (state->shared_string_array, val->v_str.val);
							g_hash_table_insert (state->shared_string_hash, val->v_str.val, tmp);
						}
						str_id = GPOINTER_TO_INT (tmp);
						type = "s";
					} else
						type = "str";
					break;
				case VALUE_CELLRANGE :
				case VALUE_ARRAY :	type = NULL; break;	/* FIXME */
				}

				if (NULL != type && *type)
					gsf_xml_out_add_cstr_unchecked (xml, "t", type);

				if (gnm_cell_has_expr (cell)) {
					texpr = cell->base.texpr;
					if (!gnm_expr_top_is_array_elem (texpr, NULL, NULL)) {
						gsf_xml_out_start_element (xml, "f");

						array = gnm_expr_top_get_array_corner (texpr);
						if (NULL != array) {
							GnmRange r;
							range_init_cellpos_size (&r, &cell->pos,
								array->cols, array->rows);
							gsf_xml_out_add_cstr_unchecked (xml, "t", "array");
							xlsx_add_range (xml, "ref", &r);
						}
						content = gnm_expr_top_as_string (cell->base.texpr,
							parse_pos_init_cell (&pp, cell), state->convs);
						gsf_xml_out_add_cstr (xml, NULL, content);
						g_free (content);

						gsf_xml_out_end_element (xml); /* </f> */
					}
				}
				if (NULL != type) {
					gsf_xml_out_start_element (xml, "v");
					if (str_id >= 0) {
						gsf_xml_out_add_int (xml, NULL, str_id);
						str_id = -1;
					} else if (val->type != VALUE_BOOLEAN) {
						GString *str = g_string_new (NULL);
						value_get_as_gstring (cell->value, str, state->convs);
						gsf_xml_out_add_cstr (xml, NULL, str->str);
						g_string_free (str, TRUE);
					} else
						xlsx_add_bool (xml, NULL, val->v_bool.val);
					gsf_xml_out_end_element (xml); /* </v> */
				}

				gsf_xml_out_end_element (xml); /* </c> */
			}
		}
		if (!needs_row)
			gsf_xml_out_end_element (xml); /* </row> */
	}
	gsf_xml_out_end_element (xml); /* </sheetData> */
	g_free (cheesy_span);;
}

static void
xlsx_write_merges (XLSXWriteState *state, GsfXMLOut *xml)
{
	GSList *ptr;

	if (NULL != (ptr = state->sheet->list_merged)) {
		gsf_xml_out_start_element (xml, "mergeCells");
		for (; ptr != NULL ; ptr = ptr->next) {
			gsf_xml_out_start_element (xml, "mergeCell");
			xlsx_add_range (xml, "ref", ptr->data);
			gsf_xml_out_end_element (xml); /* </mergeCell> */
		}
		gsf_xml_out_end_element (xml); /* </mergeCells> */
	}
}

static void
xlsx_write_validation_expr (XLSXClosure *info, GnmCellPos const *pos,
			    char const *elem, GnmExprTop const *texpr)
{
	if (NULL != texpr) {
		GnmParsePos pp;
		char *str = gnm_expr_top_as_string (texpr,
			parse_pos_init (&pp, NULL, (Sheet *)info->state->sheet,
					pos->col, pos->row),
			info->state->convs);
		gsf_xml_out_simple_element (info->xml, elem, str);
		g_free (str);
	}
}

static void
xlsx_write_validation (XLValInputPair const *vip, gpointer dummy, XLSXClosure *info)
{
#if 0
	/* Get docs on this */
	"imeMode" default="noControl"
		"noControl"
		"off"
		"on"
		"disabled"
		"hiragana"
		"fullKatakana"
		"halfKatakana"
		"fullAlpha"
		"halfAlpha"
		"fullHangul"
		"halfHangul"
#endif
	char const *tmp;

	gsf_xml_out_start_element (info->xml, "dataValidation");

	if (NULL != vip->v) {
		tmp = NULL;
		switch (vip->v->type) {
		default : /* fall back to the default */
		case VALIDATION_TYPE_ANY : /* the default "none" */  break;
		case VALIDATION_TYPE_AS_INT :		tmp = "whole"; break;
		case VALIDATION_TYPE_AS_NUMBER :	tmp = "decimal"; break;
		case VALIDATION_TYPE_IN_LIST :		tmp = "list"; break;
		case VALIDATION_TYPE_AS_DATE :		tmp = "date"; break;
		case VALIDATION_TYPE_AS_TIME :		tmp = "time"; break;
		case VALIDATION_TYPE_TEXT_LENGTH :	tmp = "textLength"; break;
		case VALIDATION_TYPE_CUSTOM :		tmp = "custom"; break;
		}
		if (NULL != tmp)
			gsf_xml_out_add_cstr_unchecked (info->xml, "type", tmp);

		tmp = NULL;
		switch (vip->v->op) {
		default : /* fall back to the default */
		case VALIDATION_OP_BETWEEN :	/* the default "between" */ break;
		case VALIDATION_OP_NOT_BETWEEN: tmp = "notBetween"; break;
		case VALIDATION_OP_EQUAL :	tmp = "equal"; break;
		case VALIDATION_OP_NOT_EQUAL :	tmp = "notEqual"; break;
		case VALIDATION_OP_LT :		tmp = "lessThan"; break;
		case VALIDATION_OP_GT :		tmp = "greaterThan"; break;
		case VALIDATION_OP_LTE :	tmp = "lessThanOrEqual"; break;
		case VALIDATION_OP_GTE :	tmp = "greaterThanOrEqual"; break;
		}
		if (NULL != tmp)
			gsf_xml_out_add_cstr_unchecked (info->xml, "operator", tmp);

		tmp = NULL;
		switch (vip->v->style) {
		default : /* fall back to the default */
		case VALIDATION_STYLE_STOP : /* "stop" the default */ break;
		case VALIDATION_STYLE_WARNING : tmp = "warning"; break;
		case VALIDATION_STYLE_INFO : tmp = "information"; break;
		}
		if (NULL != tmp)
			gsf_xml_out_add_cstr_unchecked (info->xml, "errorStyle", tmp);

		if (vip->v->allow_blank)
			xlsx_add_bool (info->xml, "allowBlank", TRUE);
		if (vip->v->use_dropdown)
			xlsx_add_bool (info->xml, "showDropDown", TRUE);

		if (NULL != vip->v->title)
			gsf_xml_out_add_cstr (info->xml, "errorTitle", vip->v->title->str);
		if (NULL != vip->v->msg)
			gsf_xml_out_add_cstr (info->xml, "error", vip->v->msg->str);
	}

	/* ?? Always TRUE but not the default ?? */
	xlsx_add_bool (info->xml, "showInputMessage", TRUE);
	xlsx_add_bool (info->xml, "showErrorMessage", TRUE);

	if (NULL != vip->msg) {
		char const *str;
		if (NULL != (str = gnm_input_msg_get_title (vip->msg)))
			gsf_xml_out_add_cstr (info->xml, "promptTitle", str);
		if (NULL != (str = gnm_input_msg_get_msg (vip->msg)))
			gsf_xml_out_add_cstr (info->xml, "prompt", str);
	}

	xlsx_add_range_list (info->xml, "sqref", vip->ranges);

	if (NULL != vip->v) {
		GnmRange const *first = vip->ranges->data;
		xlsx_write_validation_expr (info, &first->start,
			"formula1", vip->v->texpr[0]);
		xlsx_write_validation_expr (info, &first->start,
			"formula2", vip->v->texpr[1]);
	}

	gsf_xml_out_end_element (info->xml); /*  </dataValidation> */
}

static void
xlsx_write_validations (XLSXWriteState *state, GsfXMLOut *xml, GnmRange const *extent)
{
	GnmStyleList *validations = sheet_style_collect_validations (state->sheet, NULL);

	if (NULL != validations) {
		XLSXClosure info = { state, xml };
		/* filter on logical max, not extent.  XL allows validations
		 * past the stated dimension */
		GHashTable *group = excel_collect_validations (validations,
			XLSX_MAX_COLS, XLSX_MAX_ROWS);

		gsf_xml_out_start_element (xml, "dataValidations");
		gsf_xml_out_add_int (xml, "count", g_hash_table_size (group)) ;
		g_hash_table_foreach (group, (GHFunc) xlsx_write_validation, &info);
		gsf_xml_out_end_element (xml); /*  </dataValidations> */

		g_hash_table_destroy (group);
		style_list_free (validations);
	}
}

static void
xlsx_write_hlink (GnmHLink const *link, GSList *ranges, XLSXClosure *info)
{
	gchar const *target = gnm_hlink_get_target (link);
	gchar const *location = NULL;
	gchar const *rid = NULL;
	gchar const *tip;
	GType const t = G_OBJECT_TYPE (link);

	if (t == gnm_hlink_url_get_type () ||
	    t == gnm_hlink_email_get_type ()) {
		rid = gsf_outfile_open_pkg_add_extern_rel (
			GSF_OUTFILE_OPEN_PKG (gsf_xml_out_get_output (info->xml)),
			target, ns_rel_hlink);
	} else if (t != gnm_hlink_cur_wb_get_type ())
		return;

	for (; ranges  != NULL ; ranges = ranges->next) {
		gsf_xml_out_start_element (info->xml, "hyperlink");
		xlsx_add_range (info->xml, "ref", ranges->data);

		if (t == gnm_hlink_cur_wb_get_type ())
			gsf_xml_out_add_cstr (info->xml, "location", target);
		else if (NULL != rid)
			gsf_xml_out_add_cstr (info->xml, "r:id", rid);
		if (NULL != location)
			gsf_xml_out_add_cstr (info->xml, "tooltip", location);
		if (NULL != (tip = gnm_hlink_get_tip (link)))
			gsf_xml_out_add_cstr (info->xml, "tooltip", tip);
		gsf_xml_out_end_element (info->xml); /*  </hyperlink> */
	}
}

static void
xlsx_write_hlinks (XLSXWriteState *state, GsfXMLOut *xml, GnmRange const *extent)
{
	GnmStyleList *hlinks = sheet_style_collect_hlinks (state->sheet, NULL);

	if (NULL != hlinks) {
		XLSXClosure info = { state, xml };
		/* filter on logical max, not extent.  XL allows validations
		 * past the stated dimension */
		GHashTable *group = excel_collect_hlinks (hlinks,
			XLSX_MAX_COLS, XLSX_MAX_ROWS);

		gsf_xml_out_start_element (xml, "hyperlinks");
		g_hash_table_foreach (group, (GHFunc) xlsx_write_hlink, &info);
		gsf_xml_out_end_element (xml); /*  </hyperlinks> */

		g_hash_table_destroy (group);
		style_list_free (hlinks);
	}
}

static gboolean
xlsx_write_col (XLSXWriteState *state, GsfXMLOut *xml,
		ColRowInfo const *ci, int first, int last, gboolean has_child)
{
	double const def_width = state->sheet->cols.default_style.size_pts;

	if (NULL == ci)
		return has_child;

	if (!has_child)
		gsf_xml_out_start_element (xml, "cols");

	gsf_xml_out_start_element (xml, "col");
	gsf_xml_out_add_int (xml, "min", first+1) ;
	gsf_xml_out_add_int (xml, "max", last+1) ;

	gsf_xml_out_add_float (xml, "width",
		ci->size_pts / ((130. / 18.5703125) * (72./96.)), 7);
	if (!ci->visible)
		gsf_xml_out_add_cstr_unchecked (xml, "hidden", "1");
	if (ci->hard_size)
		gsf_xml_out_add_cstr_unchecked (xml, "customWidth", "1");
	else if (fabs (def_width - ci->size_pts) > .1) {
		gsf_xml_out_add_cstr_unchecked (xml, "bestFit", "1");
		gsf_xml_out_add_cstr_unchecked (xml, "customWidth", "1");
	}

	if (ci->outline_level > 0)
		gsf_xml_out_add_int (xml, "outlineLevel", ci->outline_level);
	if (ci->is_collapsed)
		gsf_xml_out_add_cstr_unchecked (xml, "collapsed", "1");
	gsf_xml_out_end_element (xml); /* </col> */

	return TRUE;
}

static void
xlsx_write_cols (XLSXWriteState *state, GsfXMLOut *xml, GnmRange const *extent)
{
	ColRowInfo const *ci, *info;
	gboolean has_child = FALSE;
	int first_col = -1, i;

	do {
		info = sheet_col_get (state->sheet, ++first_col);
	} while (info == NULL && first_col < extent->end.col);

	if (info == NULL)
		return;

	for (i = first_col + 1; i <= extent->end.col ; i++) {
		ci = sheet_col_get (state->sheet, i);
		if (!colrow_equal (info, ci)) {
			has_child |= xlsx_write_col (state, xml, info, first_col, i-1, has_child);
			info	  = ci;
			first_col = i;
		}
	}
	has_child |= xlsx_write_col (state, xml, info, first_col, i-1, has_child);

	if (has_child)
		gsf_xml_out_end_element (xml); /* </cols> */
}

static void
xlsx_write_autofilters (XLSXWriteState *state, GsfXMLOut *xml)
{
	GnmFilter const *filter;
	GnmFilterCondition const *cond;
	unsigned i;

	if (NULL == state->sheet->filters)
		return;

	filter = state->sheet->filters->data;
	gsf_xml_out_start_element (xml, "autoFilter");
	xlsx_add_range (xml, "ref", &filter->r);

	for (i = 0; i < filter->fields->len ; i++) {
		/* filter unused or bucket filters in excel5 */
		if (NULL == (cond = gnm_filter_get_condition (filter, i)) ||
		    cond->op[0] == GNM_FILTER_UNUSED)
			continue;

		gsf_xml_out_start_element (xml, "filterColumn");
		gsf_xml_out_add_int (xml, "colId", i);

		switch (cond->op[0]) {
		case GNM_FILTER_OP_EQUAL :
		case GNM_FILTER_OP_GT :
		case GNM_FILTER_OP_LT :
		case GNM_FILTER_OP_GTE :
		case GNM_FILTER_OP_LTE :
		case GNM_FILTER_OP_NOT_EQUAL :
			break;

		case GNM_FILTER_OP_BLANKS :
		case GNM_FILTER_OP_NON_BLANKS :
			break;

		case GNM_FILTER_OP_TOP_N :
		case GNM_FILTER_OP_BOTTOM_N :
		case GNM_FILTER_OP_TOP_N_PERCENT :
		case GNM_FILTER_OP_BOTTOM_N_PERCENT :
			gsf_xml_out_start_element (xml, "top10");
			gsf_xml_out_add_float (xml, "val", cond->count, -1);
			if (cond->op[0] & GNM_FILTER_OP_BOTTOM_MASK)
				gsf_xml_out_add_cstr_unchecked (xml, "top", "0");
			if (cond->op[0] & GNM_FILTER_OP_PERCENT_MASK)
				gsf_xml_out_add_cstr_unchecked (xml, "percent", "1");
			gsf_xml_out_end_element (xml); /* </top10> */
			break;

		default :
			continue;
		}

		gsf_xml_out_end_element (xml); /* </filterColumn> */
	}
	gsf_xml_out_end_element (xml); /* </autoFilter> */
}

static void
xlsx_write_protection (XLSXWriteState *state, GsfXMLOut *xml)
{
	gboolean sheet;
	gboolean objects;
	gboolean scenarios;
	gboolean formatCells;
	gboolean formatColumns;
	gboolean formatRows;
	gboolean insertColumns;
	gboolean insertRows;
	gboolean insertHyperlinks;
	gboolean deleteColumns;
	gboolean deleteRows;
	gboolean selectLockedCells;
	gboolean sort;
	gboolean autoFilter;
	gboolean pivotTables;
	gboolean selectUnlockedCells;

	g_object_get (G_OBJECT (state->sheet),
		"protected",				 &sheet,
		"protected-allow-edit-objects",		 &objects,
		"protected-allow-edit-scenarios",	 &scenarios,
		"protected-allow-cell-formatting",	 &formatCells,
		"protected-allow-column-formatting",	 &formatColumns,
		"protected-allow-row-formatting",	 &formatRows,
		"protected-allow-insert-columns",	 &insertColumns,
		"protected-allow-insert-rows",		 &insertRows,
		"protected-allow-insert-hyperlinks",	 &insertHyperlinks,
		"protected-allow-delete-columns",	 &deleteColumns,
		"protected-allow-delete-rows",		 &deleteRows,
		"protected-allow-select-locked-cells",	 &selectLockedCells,
		"protected-allow-sort-ranges",		 &sort,
		"protected-allow-edit-auto-filters",	 &autoFilter,
		"protected-allow-edit-pivottable",	 &pivotTables,
		"protected-allow-select-unlocked-cells", &selectUnlockedCells,
		NULL);

	gsf_xml_out_start_element (xml, "sheetProtection");
	if ( sheet)		  xlsx_add_bool (xml, "sheet",			TRUE);
	if ( objects)		  xlsx_add_bool (xml, "objects",		TRUE);
	if ( scenarios)		  xlsx_add_bool (xml, "scenarios",		TRUE);
	if (!formatCells)	  xlsx_add_bool (xml, "formatCells",		FALSE);
	if (!formatColumns)	  xlsx_add_bool (xml, "formatColumns",		FALSE);
	if (!formatRows)	  xlsx_add_bool (xml, "formatRows",		FALSE);
	if (!insertColumns)	  xlsx_add_bool (xml, "insertColumns",		FALSE);
	if (!insertRows)	  xlsx_add_bool (xml, "insertRows",		FALSE);
	if (!insertHyperlinks)	  xlsx_add_bool (xml, "insertHyperlinks",	FALSE);
	if (!deleteColumns)	  xlsx_add_bool (xml, "deleteColumns",		FALSE);
	if (!deleteRows)	  xlsx_add_bool (xml, "deleteRows",		FALSE);
	if ( selectLockedCells)	  xlsx_add_bool (xml, "selectLockedCells",	TRUE);
	if (!sort)		  xlsx_add_bool (xml, "sort",			FALSE);
	if (!autoFilter)	  xlsx_add_bool (xml, "autoFilter",		FALSE);
	if (!pivotTables)	  xlsx_add_bool (xml, "pivotTables",		FALSE);
	if ( selectUnlockedCells) xlsx_add_bool (xml, "selectUnlockedCells",	TRUE);

	gsf_xml_out_end_element (xml); /* sheetProtection */
}

static void
xlsx_write_breaks (XLSXWriteState *state, GsfXMLOut *xml, GnmPageBreaks *breaks)
{
	unsigned const maxima = (breaks->is_vert ? XLSX_MaxCol : XLSX_MaxRow) - 1;
	GArray const *details = breaks->details;
	GnmPageBreak const *binfo;
	unsigned i;

	gsf_xml_out_start_element (xml,
		(breaks->is_vert) ? "rowBreaks" : "colBreaks");
	gsf_xml_out_add_int (xml, "count", details->len);

	for (i = 0 ; i < details->len ; i++) {
		binfo = &g_array_index (details, GnmPageBreak, i);
		gsf_xml_out_start_element (xml, "brk");
		gsf_xml_out_add_int (xml, "id", binfo->pos);

		/* hard code min=0 max=dir */
		gsf_xml_out_add_int (xml, "max", maxima);

		switch (binfo->type) {
		case GNM_PAGE_BREAK_MANUAL :	gsf_xml_out_add_bool (xml, "man", TRUE); break;
		case GNM_PAGE_BREAK_AUTO :	break;
		case GNM_PAGE_BREAK_NONE :	break;
		case GNM_PAGE_BREAK_DATA_SLICE :gsf_xml_out_add_bool (xml, "pt", TRUE); break;
		}
		gsf_xml_out_end_element (xml); /* </brk> */
	}
	gsf_xml_out_end_element (xml);
}

static void
xlsx_write_print_info (XLSXWriteState *state, GsfXMLOut *xml)
{
	PrintInformation *pi = state->sheet->print_info;
	double h_margin, f_margin;
	double left;
	double right;
	double t_margin, b_margin;

	g_return_if_fail (pi != NULL);

	gsf_xml_out_start_element (xml, "printOptions");
	gsf_xml_out_end_element (xml); /* </printOptions> */

	gsf_xml_out_start_element (xml, "pageMargins");
	print_info_get_margins (pi, &h_margin, &f_margin, &left, &right,
				&t_margin, &b_margin);
	gsf_xml_out_add_float (xml, "left",	left / 72., 4);
	gsf_xml_out_add_float (xml, "right",	right / 72., 4);
	gsf_xml_out_add_float (xml, "top",	t_margin / 72., 4);
	gsf_xml_out_add_float (xml, "bottom",	b_margin / 72., 4);
	gsf_xml_out_add_float (xml, "header",	h_margin / 72., 4);
	gsf_xml_out_add_float (xml, "footer",	f_margin / 72., 4);
	gsf_xml_out_end_element (xml); /* </pageMargins> */

	gsf_xml_out_start_element (xml, "pageSetup");
	gsf_xml_out_end_element (xml); /* </pageSetup> */

	if (NULL != pi->page_breaks.v)
		xlsx_write_breaks (state, xml, pi->page_breaks.v);
	if (NULL != pi->page_breaks.h)
		xlsx_write_breaks (state, xml, pi->page_breaks.h);

#if 0
	gsf_xml_out_start_element (xml, "headerFooter");
	gsf_xml_out_end_element (xml); /* </headerFooter> */
#endif

}

/**********************************************************************/

static void
xlsx_write_rich_text (GsfXMLOut *xml, char const *text, PangoAttrList *attrs)
{
	PangoAttrIterator *iter =  pango_attr_list_get_iterator (attrs);
	PangoAttribute *attr;
	int start, end, max = strlen (text);
	char *buf;
	do {
		gsf_xml_out_start_element (xml, "r");
		gsf_xml_out_start_element (xml, "rPr");
		gsf_xml_out_start_element (xml, "rFont");
		attr = pango_attr_iterator_get (iter, PANGO_ATTR_FAMILY);
		gsf_xml_out_add_cstr_unchecked (xml, "val", (attr)? ((PangoAttrString *) attr)->value: "Calibri");
		gsf_xml_out_end_element (xml); /* </rFont> */
		gsf_xml_out_start_element (xml, "b");
		attr = pango_attr_iterator_get (iter, PANGO_ATTR_WEIGHT);
		gsf_xml_out_add_cstr_unchecked (xml, "val", (attr && ((PangoAttrInt *) attr)->value > PANGO_WEIGHT_NORMAL)? "true": "false");
		gsf_xml_out_end_element (xml); /* </b> */
		gsf_xml_out_start_element (xml, "i");
		attr = pango_attr_iterator_get (iter, PANGO_ATTR_STYLE);
		gsf_xml_out_add_cstr_unchecked (xml, "val", (attr && ((PangoAttrInt *) attr)->value != PANGO_STYLE_NORMAL)? "true": "false");
		gsf_xml_out_end_element (xml); /* </i> */
		gsf_xml_out_start_element (xml, "strike");
		attr = pango_attr_iterator_get (iter, PANGO_ATTR_STRIKETHROUGH);
		gsf_xml_out_add_cstr_unchecked (xml, "val", (attr && ((PangoAttrInt *) attr)->value)? "true": "false");
		gsf_xml_out_end_element (xml); /* </strike> */
		gsf_xml_out_start_element (xml, "color");
		attr = pango_attr_iterator_get (iter, PANGO_ATTR_FOREGROUND);
		if (attr) {
			PangoColor *color = &((PangoAttrColor *) attr)->color;
			buf = g_strdup_printf("FF%2x%2x%2x", color->red >> 8, color->green >> 8, color->blue >> 8);
			gsf_xml_out_add_cstr_unchecked (xml, "rgb", buf);
			g_free (buf);
		} else
			gsf_xml_out_add_cstr_unchecked (xml, "rgb", "FF000000");
		gsf_xml_out_end_element (xml); /* </color> */
		gsf_xml_out_start_element (xml, "sz");
		attr = pango_attr_iterator_get (iter, PANGO_ATTR_SIZE);
		gsf_xml_out_add_uint (xml, "val", (attr)? ((PangoAttrInt *) attr)->value / PANGO_SCALE: 8);
		gsf_xml_out_end_element (xml); /* </sz> */
		gsf_xml_out_start_element (xml, "u");
		attr = pango_attr_iterator_get (iter, PANGO_ATTR_UNDERLINE);
		if (attr) {
			PangoUnderline u = ((PangoAttrInt *) attr)->value;
			switch (u) {
			case PANGO_UNDERLINE_NONE:
			default:
				gsf_xml_out_add_cstr_unchecked (xml, "val", "none");
				break;
			case PANGO_UNDERLINE_ERROR: /* not supported by OpenXML */
			case PANGO_UNDERLINE_SINGLE:
				gsf_xml_out_add_cstr_unchecked (xml, "val", "single");
			case PANGO_UNDERLINE_DOUBLE:
				gsf_xml_out_add_cstr_unchecked (xml, "val", "double");
			case PANGO_UNDERLINE_LOW:
				gsf_xml_out_add_cstr_unchecked (xml, "val", "singleAccounting");
			}
		} else
			gsf_xml_out_add_cstr_unchecked (xml, "val", "none");
		gsf_xml_out_end_element (xml); /* </u> */
		gsf_xml_out_end_element (xml); /* </rPr> */
		gsf_xml_out_start_element (xml, "t");
		gsf_xml_out_add_cstr_unchecked (xml, "xml:space", "preserve");
		pango_attr_iterator_range (iter, &start, &end);
		if (end > max)
		    end = max;
		buf = g_strndup (text + start, end - start);
		gsf_xml_out_add_cstr_unchecked (xml, NULL, buf);
		g_free (buf);
		gsf_xml_out_end_element (xml); /* </t> */
		gsf_xml_out_end_element (xml); /* </r> */
	} while (pango_attr_iterator_next (iter));
	pango_attr_iterator_destroy (iter);
}

static void
write_comment_author (gpointer key, G_GNUC_UNUSED gpointer value, GsfXMLOut *xml)
{
	gsf_xml_out_start_element (xml, "author");
	gsf_xml_out_add_cstr_unchecked (xml, NULL, (char const *) key);
	gsf_xml_out_end_element (xml);
}

static void
xlsx_write_comments (XLSXWriteState *state, GsfOutput *sheet_part, GSList *objects)
{
	GsfXMLOut *xml;
	GHashTable *authors;
	unsigned author = 0;
	char const *authorname;
	GSList *ptr;
	SheetObjectAnchor const *anchor;
	PangoAttrList *attrs;
	char *name = g_strdup_printf ("comments%u.xml", ++state->comment);
	GsfOutput *comments_part = gsf_outfile_new_child_full (state->xl_dir, name, FALSE,
		"content-type", "application/vnd.openxmlformats-officedocument.spreadsheetml.comments+xml",
		NULL);
	g_free (name);
	gsf_outfile_open_pkg_relate (GSF_OUTFILE_OPEN_PKG (comments_part),
		GSF_OUTFILE_OPEN_PKG (sheet_part), ns_rel_com);
	xml = gsf_xml_out_new (comments_part);
	gsf_xml_out_start_element (xml, "comments");
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns", ns_ss);
	/* search for comments authors */
	authors = g_hash_table_new (g_str_hash, g_str_equal);
	for (ptr = objects; ptr; ptr = ptr->next) {
		authorname = cell_comment_author_get (CELL_COMMENT (ptr->data));
		if (authorname != NULL && !g_hash_table_lookup_extended (authors, authorname, NULL, NULL))
			g_hash_table_insert (authors, (gpointer) authorname, GUINT_TO_POINTER (author++));
	}
	/* save authors */
	gsf_xml_out_start_element (xml, "authors");
	g_hash_table_foreach (authors, (GHFunc) write_comment_author, xml);
	gsf_xml_out_end_element (xml); /* </authors> */
	/* save comments */
	gsf_xml_out_start_element (xml, "commentList");
	for (ptr = objects; ptr; ptr = ptr->next) {
		gsf_xml_out_start_element (xml, "comment");
		anchor = sheet_object_get_anchor (ptr->data);
		gsf_xml_out_add_cstr_unchecked (xml, "ref", range_as_string (&anchor->cell_bound));
		authorname = cell_comment_author_get (CELL_COMMENT (ptr->data));
		if (authorname != NULL)
			gsf_xml_out_add_uint (xml, "authorId", 
					      GPOINTER_TO_UINT (g_hash_table_lookup (authors, authorname)));
		gsf_xml_out_start_element (xml, "text");
		/* Save text as rich text */
		g_object_get (ptr->data, "text", &name, "markup", &attrs, NULL);
		if (name && *name)
			xlsx_write_rich_text (xml, name, attrs);
		g_free (name);
		pango_attr_list_unref (attrs);
		gsf_xml_out_end_element (xml); /* </text> */
		gsf_xml_out_end_element (xml); /* </comment> */
	}
	gsf_xml_out_end_element (xml); /* </commentList> */
	g_hash_table_destroy (authors);
	gsf_xml_out_end_element (xml); /* </comments> */
	g_object_unref (xml);
	gsf_output_close (comments_part);
	g_object_unref (comments_part);
}

#include "xlsx-write-drawing.c"

static char const *
xlsx_write_sheet (XLSXWriteState *state, GsfOutfile *dir, GsfOutfile *wb_part, unsigned i)
{
	char *name = g_strdup_printf ("sheet%u.xml", i+1);
	GsfOutput *sheet_part = gsf_outfile_new_child_full (dir, name, FALSE,
		"content-type", "application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml",
		NULL);
	char const *rId = gsf_outfile_open_pkg_relate (GSF_OUTFILE_OPEN_PKG (sheet_part),
		GSF_OUTFILE_OPEN_PKG (wb_part),
		"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet");
	GsfXMLOut *xml;
	GnmRange  extent;
	GSList   *charts;
	char const *chart_drawing_rel_id = NULL;
	GnmStyle **col_styles;

	state->sheet = workbook_sheet_by_index (state->base.wb, i);
	col_styles = g_new (GnmStyle*, MIN (XLSX_MAX_COLS,
					    gnm_sheet_get_max_cols (state->sheet)));
	excel_sheet_extent (state->sheet, &extent, col_styles,
		MIN (XLSX_MAX_COLS, gnm_sheet_get_max_cols (state->sheet)),
		MIN (XLSX_MAX_ROWS, gnm_sheet_get_max_rows (state->sheet)), state->io_context);

/*   comments   */
	charts = sheet_objects_get (state->sheet, NULL, CELL_COMMENT_TYPE);
	if (NULL != charts) {
		xlsx_write_comments (state, sheet_part, charts);
		g_slist_free (charts);
	}

/*   charts   */
	charts = sheet_objects_get (state->sheet, NULL, SHEET_OBJECT_GRAPH_TYPE);
	if (NULL != charts) {
		chart_drawing_rel_id = xlsx_write_objects (state, sheet_part, charts);
		g_slist_free (charts);
	}

	xml = gsf_xml_out_new (sheet_part);
/* CT_Worksheet =                                          */
	gsf_xml_out_start_element (xml, "worksheet");
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns", ns_ss);
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns:r", ns_rel);

	/* for now we only use tabColor, move sheetPr outside when we add more
	 * features */
	if (NULL != state->sheet->tab_color) {
/*   element sheetPr { CT_SheetPr }?,     */
		gsf_xml_out_start_element (xml, "sheetPr");

		gsf_xml_out_start_element (xml, "tabColor");
		xlsx_add_rgb (xml, "rgb", state->sheet->tab_color->go_color);
		gsf_xml_out_end_element (xml); /* </tabColor> */

		gsf_xml_out_end_element (xml); /* </sheetPr> */
	}
/*   element dimension { CT_SheetDimension }?,     */
	gsf_xml_out_start_element (xml, "dimension");
	xlsx_add_range (xml, "ref", &extent);
	gsf_xml_out_end_element (xml); /* </dimension> */
/*   element sheetViews { CT_SheetViews }?,     */
	gsf_xml_out_start_element (xml, "sheetViews");
	SHEET_FOREACH_VIEW (state->sheet, sv, xlsx_write_sheet_view (xml, sv););
	gsf_xml_out_end_element (xml); /* </sheetViews> */
/*   element sheetFormatPr { CT_SheetFormatPr }?,     */
	gsf_xml_out_start_element (xml, "sheetFormatPr");
	gsf_xml_out_add_float (xml, "defaultRowHeight",
		sheet_row_get_default_size_pts (state->sheet), 4);
	if (state->sheet->rows.max_outline_level > 0)
		gsf_xml_out_add_int (xml, "outlineLevelRow",
			state->sheet->rows.max_outline_level);
	if (state->sheet->cols.max_outline_level > 0)
		gsf_xml_out_add_int (xml, "outlineLevelCol",
			state->sheet->cols.max_outline_level);
	gsf_xml_out_end_element (xml); /* </sheetFormatPr> */
/*   element cols { CT_Cols }*,     */
	xlsx_write_cols (state, xml, &extent);
/*   element sheetData { CT_SheetData },     */
	xlsx_write_cells (state, xml, &extent);
/*   element sheetCalcPr { CT_SheetCalcPr }?,     */
/*   element sheetProtection { CT_SheetProtection }?,     */
	xlsx_write_protection (state, xml);
/*   element protectedRanges { CT_ProtectedRanges }?,     */
/*   element scenarios { CT_Scenarios }?,     */
/*   element autoFilter { CT_AutoFilter }?,     */
	xlsx_write_autofilters (state, xml);
/*   element sortState { CT_SortState }?,     */
/*   element dataConsolidate { CT_DataConsolidate }?,     */
/*   element customSheetViews { CT_CustomSheetViews }?,     */
/*   element mergeCells { CT_MergeCells }?,     */
	xlsx_write_merges (state, xml);
/*   element phoneticPr { CT_PhoneticPr }?,     */
/*   element conditionalFormatting { CT_ConditionalFormatting }*,     */
/*   element dataValidations { CT_DataValidations }?,     */
	xlsx_write_validations (state, xml, &extent);
/*   element hyperlinks { CT_Hyperlinks }?,     */
	xlsx_write_hlinks (state, xml, &extent);
/*   element printOptions { CT_PrintOptions }?, included in xlsx_write_print_info */
/*   element pageMargins { CT_PageMargins }?,   included in xlsx_write_print_info */
/*   element pageSetup { CT_PageSetup }?,       included in xlsx_write_print_info */
/*   element headerFooter { CT_HeaderFooter }?, included in xlsx_write_print_info */
	xlsx_write_print_info (state, xml);
/*   element rowBreaks { CT_PageBreak }?,     */
/*   element colBreaks { CT_PageBreak }?,     */
/*   element customProperties { CT_CustomProperties }?,     */
/*   element cellWatches { CT_CellWatches }?,     */
/*   element ignoredErrors { CT_IgnoredErrors }?,     */
/*   element smartTags { CT_SmartTags }?,     */
/*   element drawing { CT_Drawing }?,     */
	if (NULL != chart_drawing_rel_id) {
		gsf_xml_out_start_element (xml, "drawing");
		gsf_xml_out_add_cstr_unchecked (xml, "r:id", chart_drawing_rel_id);
		gsf_xml_out_end_element (xml);  /* </drawing> */
	}
/*   element legacyDrawing { CT_LegacyDrawing }?,     */
/*   element legacyDrawingHF { CT_LegacyDrawing }?,     */
/*   element picture { CT_SheetBackgroundPicture }?,     */
/*   element oleObjects { CT_OleObjects }?,     */
/*   element controls { CT_Controls }?,     */
/*   element webPublishItems { CT_WebPublishItems }?,     */
/*   element tableParts { CT_TableParts }?,     */
/*   element extLst { CT_ExtensionList }?     */
	gsf_xml_out_end_element (xml); /* </worksheet> */

	g_object_unref (xml);
	gsf_output_close (sheet_part);
	g_object_unref (sheet_part);
	g_free (name);
	g_free (col_styles);

	state->sheet = NULL;

	return rId;
}

static void
xlsx_write_calcPR (XLSXWriteState *state, GsfXMLOut *xml)
{
	Workbook const *wb = state->base.wb;

#warning Filter by defaults
	gsf_xml_out_start_element (xml, "calcPr");

	gsf_xml_out_add_cstr_unchecked (xml, "calcMode",
		wb->recalc_auto ? "auto" : "manual");

	xlsx_add_bool (xml, "iterate", wb->iteration.enabled);
	gsf_xml_out_add_int (xml, "iterateCount",
		wb->iteration.max_number);
	gsf_xml_out_add_float (xml, "iterateDelta",
		wb->iteration.tolerance, -1);

	gsf_xml_out_end_element (xml);
}

#include "xlsx-write-pivot.c"

static void
xlsx_write_workbook (XLSXWriteState *state, GsfOutfile *root_part)
{
	int i;
	GsfXMLOut  *xml;
	GSList	   *cacheRefs;
	GPtrArray  *sheetIds  = g_ptr_array_new ();
	GsfOutfile *xl_dir    = (GsfOutfile *)gsf_outfile_new_child (root_part, "xl", TRUE);
	GsfOutfile *sheet_dir = (GsfOutfile *)gsf_outfile_new_child (xl_dir, "worksheets", TRUE);
	GsfOutfile *wb_part   = (GsfOutfile *)gsf_outfile_open_pkg_add_rel (xl_dir, "workbook.xml",
		"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml",
		root_part,
		"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument");

	state->xl_dir = xl_dir;
	state->shared_string_hash = g_hash_table_new (g_direct_hash, g_direct_equal);
	state->shared_string_array = g_ptr_array_new ();
	state->convs	 = xlsx_conventions_new ();
	state->chart.dir   = state->drawing.dir   = NULL;
	state->chart.count = state->drawing.count = 0;

	g_ptr_array_set_size (sheetIds, workbook_sheet_count (state->base.wb));
	for (i = 0 ; i < workbook_sheet_count (state->base.wb); i++)
		g_ptr_array_index (sheetIds, i) =
			(gpointer) xlsx_write_sheet (state, sheet_dir, wb_part, i);

	xlsx_write_shared_strings (state, wb_part);
	xlsx_write_styles (state, wb_part);
	cacheRefs = xlsx_write_pivots (state, wb_part);

	xml = gsf_xml_out_new (GSF_OUTPUT (wb_part));
	gsf_xml_out_start_element (xml, "workbook");
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns", ns_ss);
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns:r", ns_rel);
	gsf_xml_out_add_cstr_unchecked (xml, "xml:space", "preserve");

	gsf_xml_out_start_element (xml, "fileVersion");
	gsf_xml_out_add_int (xml, "lastEdited", 4);
	gsf_xml_out_add_int (xml, "lowestEdited", 4);
	gsf_xml_out_add_int (xml, "rupBuild", 3820);
	gsf_xml_out_end_element (xml);

	gsf_xml_out_start_element (xml, "workbookPr");
	gsf_xml_out_add_int (xml, "date1904",
			     workbook_date_conv (state->base.wb)->use_1904
			     ? 1 : 0);
	gsf_xml_out_end_element (xml);

	gsf_xml_out_start_element (xml, "bookViews");
	WORKBOOK_FOREACH_VIEW (state->base.wb, view, {
		gsf_xml_out_start_element (xml, "workbookView");
		gsf_xml_out_add_int (xml, "activeTab",
			view->current_sheet->index_in_wb);
		gsf_xml_out_end_element (xml);
	});
	gsf_xml_out_end_element (xml);

	gsf_xml_out_start_element (xml, "sheets");
	for (i = 0 ; i < workbook_sheet_count (state->base.wb); i++) {
		Sheet const *sheet = workbook_sheet_by_index (state->base.wb, i);
		gsf_xml_out_start_element (xml, "sheet");
		gsf_xml_out_add_cstr (xml, "name", sheet->name_unquoted);
		gsf_xml_out_add_int (xml, "sheetId", i+1);	/* FIXME What is this ?? */
		gsf_xml_out_add_cstr_unchecked (xml, "r:id",
			g_ptr_array_index (sheetIds, i));
		gsf_xml_out_end_element (xml); /* </sheet> */
	}
	gsf_xml_out_end_element (xml); /* </sheets> */

	xlsx_write_calcPR (state, xml);

	if (NULL != cacheRefs) {
		GSList *ptr;
		unsigned int i = 0;
		gsf_xml_out_start_element (xml, "pivotCaches");
		for (ptr = cacheRefs ; ptr != NULL ; ptr = ptr->next) {
			gsf_xml_out_start_element (xml, "pivotCache");
			gsf_xml_out_add_int (xml, "cacheId", i++);
			gsf_xml_out_add_cstr_unchecked (xml, "r:id", ptr->data);
			gsf_xml_out_end_element (xml); /* </pivotCache> */
		}
		gsf_xml_out_end_element (xml); /* </pivotCaches> */
	}
	gsf_xml_out_start_element (xml, "webPublishing");
	gsf_xml_out_add_int (xml, "codePage", 1252);	/* FIXME : Use utf-8 ? */
	gsf_xml_out_end_element (xml);

	gsf_xml_out_end_element (xml); /* </workbook> */
	g_object_unref (xml);

	xlsx_conventions_free (state->convs);
	g_hash_table_destroy (state->shared_string_hash);
	g_ptr_array_free (state->shared_string_array, TRUE);

	if (NULL != state->chart.dir)
		gsf_output_close (GSF_OUTPUT (state->chart.dir));
	if (NULL != state->drawing.dir)
		gsf_output_close (GSF_OUTPUT (state->drawing.dir));
	gsf_output_close (GSF_OUTPUT (wb_part));
	g_ptr_array_free (sheetIds, TRUE);
	gsf_output_close (GSF_OUTPUT (sheet_dir));
	gsf_output_close (GSF_OUTPUT (xl_dir));
}

G_MODULE_EXPORT void
xlsx_file_save (GOFileSaver const *fs, GOIOContext *io_context,
		gconstpointer wb_view, GsfOutput *output);
void
xlsx_file_save (GOFileSaver const *fs, GOIOContext *io_context,
		gconstpointer wb_view, GsfOutput *output)
{
	XLSXWriteState state;
	GsfOutfile *root_part;
	GnmLocale  *locale;

	locale = gnm_push_C_locale ();

	state.io_context	= io_context;
	state.base.wb		= wb_view_get_workbook (wb_view);
	state.comment		= 0;
	root_part = gsf_outfile_open_pkg_new (
		gsf_outfile_zip_new (output, NULL));

	xlsx_write_workbook (&state, root_part);
	gsf_output_close (GSF_OUTPUT (root_part));
	g_object_unref (root_part);

	gnm_pop_C_locale (locale);
}

/* TODO : (Just about everything)
 *	Figure out why XL 12 complains about cells and cols
 *	styles
 *	rich text
 *	shared expressions
 *	external refs
 *	charts
 *	...
 *	*/

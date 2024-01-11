/*
 * xml-sax-write.c : export .gnumeric and the clipboard subset using the sax
 *			like wrappers in libgsf
 *
 * Copyright (C) 2003-2007 Jody Goldberg (jody@gnome.org)
 * Copyright (C) 2024 Morten Welinder <terra@gnome.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) version 3.
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
#include <gnumeric.h>
#include <glib/gi18n-lib.h>
#include <xml-sax.h>
#include <workbook-view.h>
#include <gnm-format.h>
#include <workbook.h>
#include <workbook-priv.h>
#include <cell.h>
#include <sheet.h>
#include <sheet-view.h>
#include <sheet-style.h>
#include <style-color.h>
#include <style-conditions.h>
#include <expr.h>
#include <expr-name.h>
#include <value.h>
#include <ranges.h>
#include <mstyle.h>
#include <style-border.h>
#include <validation.h>
#include <hlink.h>
#include <input-msg.h>
#include <tools/gnm-solver.h>
#include <sheet-filter.h>
#include <sheet-object-impl.h>
#include <sheet-object-cell-comment.h>
#include <print-info.h>
#include <gutils.h>
#include <clipboard.h>
#include <tools/scenarios.h>
#include <gnumeric-conf.h>

#include <goffice/goffice.h>
#include <gsf/gsf-libxml.h>
#include <gsf/gsf-output-gzip.h>
#include <gsf/gsf-doc-meta-data.h>
#include <gsf/gsf-opendoc-utils.h>
#include <gsf/gsf-utils.h>

typedef struct {
	WorkbookView const *wb_view;	/* View for the new workbook */
	Workbook const	   *wb;		/* The new workbook */
	Sheet const	   *sheet;
	GnmConventions	   *convs;
	GHashTable	   *expr_map;
	GString		   *cell_str;   /* Scratch pad.  */

	// Do we write result values?  For now this is clipboard only
	gboolean            write_value_result;

	GsfXMLOut *output;
} GnmOutputXML;

#define GNM "gnm:"

/* Precision to use when saving point measures. */
#define POINT_SIZE_PRECISION 4

void
gnm_xml_out_add_gocolor (GsfXMLOut *o, char const *id, GOColor c)
{
	/*
	 * This uses format "rrrr:gggg:bbbb" or "rrrr:gggg:bbbb:aaaa"
	 * using hex numbers, i.e., the numbers are in the range from
	 * 0 to FFFF.
	 *
	 * Note, that while go_xml_out_add_color exists, we cannot use
	 * it as it using a 0-255 scaling and always includes alpha.
	 */
	unsigned r, g, b, a;
	char buf[4 * 4 * sizeof (unsigned int) + 1];

	GO_COLOR_TO_RGBA (c, &r, &g, &b, &a);

	sprintf (buf, "%X:%X:%X%c%X",
		 r * 0x101, g * 0x101, b * 0x101,
		 (a == 0xff ? 0 : ':'),
		 a * 0x101);
	gsf_xml_out_add_cstr_unchecked (o, id, buf);
}

static void
gnm_xml_out_add_color (GsfXMLOut *o, char const *id, GnmColor const *c)
{
	gnm_xml_out_add_gocolor (o, id, c->go_color);
}

static void
gnm_xml_out_add_cellpos (GsfXMLOut *o, char const *id, GnmCellPos const *p)
{
	g_return_if_fail (p != NULL);
	gsf_xml_out_add_cstr_unchecked (o, id, cellpos_as_string (p));
}

static void
xml_out_add_range (GsfXMLOut *xml, GnmRange const *r)
{
	g_return_if_fail (range_is_sane (r));

	gsf_xml_out_add_int (xml, "startCol", r->start.col);
	gsf_xml_out_add_int (xml, "startRow", r->start.row);
	gsf_xml_out_add_int (xml, "endCol",   r->end.col);
	gsf_xml_out_add_int (xml, "endRow",   r->end.row);
}

static void
xml_out_add_points (GsfXMLOut *xml, char const *name, double val)
{
	gsf_xml_out_add_float (xml, name, val, POINT_SIZE_PRECISION);
}

static void
xml_write_boolean_attribute (GnmOutputXML *state, char const *name, gboolean value)
{
	gsf_xml_out_start_element (state->output, GNM "Attribute");
	gsf_xml_out_simple_element (state->output, GNM "name", name);
	gsf_xml_out_simple_element (state->output, GNM "value", value ? "TRUE" : "FALSE");
	gsf_xml_out_end_element (state->output); /* </Attribute> */
}

static void
xml_write_version (GnmOutputXML *state)
{
	gsf_xml_out_start_element (state->output, GNM "Version");
	gsf_xml_out_add_int (state->output, "Epoch", GNM_VERSION_EPOCH);
	gsf_xml_out_add_int (state->output, "Major", GNM_VERSION_MAJOR);
	gsf_xml_out_add_int (state->output, "Minor", GNM_VERSION_MINOR);
	gsf_xml_out_add_cstr_unchecked (state->output, "Full", GNM_VERSION_FULL);
	gsf_xml_out_end_element (state->output); /* </Version> */
}

static void
xml_write_attributes (GnmOutputXML *state)
{
	gsf_xml_out_start_element (state->output, GNM "Attributes");
	xml_write_boolean_attribute
		(state, "WorkbookView::show_horizontal_scrollbar",
		 state->wb_view->show_horizontal_scrollbar);
	xml_write_boolean_attribute
		(state, "WorkbookView::show_vertical_scrollbar",
		 state->wb_view->show_vertical_scrollbar);
	xml_write_boolean_attribute
		(state, "WorkbookView::show_notebook_tabs",
		 state->wb_view->show_notebook_tabs);
	xml_write_boolean_attribute
		(state, "WorkbookView::do_auto_completion",
		 state->wb_view->do_auto_completion);
	xml_write_boolean_attribute
		(state, "WorkbookView::is_protected",
		 state->wb_view->is_protected);
	gsf_xml_out_end_element (state->output); /* </Attributes> */
}

static void
xml_write_meta_data (GnmOutputXML *state)
{
	gsf_doc_meta_data_write_to_odf (go_doc_get_meta_data (GO_DOC (state->wb)),
	                            state->output);
}

/* DEPRECATED in 1.7.11 */
static void
xml_write_conventions (GnmOutputXML *state)
{
	GODateConventions const *conv = workbook_date_conv (state->wb);
	if (conv->use_1904)
		gsf_xml_out_simple_element (state->output, GNM "DateConvention", "1904");
}

static void
xml_write_sheet_names (GnmOutputXML *state)
{
	int i, n = workbook_sheet_count (state->wb);
	Sheet *sheet;

	gsf_xml_out_start_element (state->output, GNM "SheetNameIndex");
	for (i = 0 ; i < n ; i++) {
		sheet = workbook_sheet_by_index (state->wb, i);
		gsf_xml_out_start_element (state->output, GNM "SheetName");

		/*
		 * Note, that we explicitly namespace these attributes.
		 * That is not wrong, per se, but note that Gnumeric until
		 * 1.12.22 will not read files without this explicit name-
		 * space and that the abbreviation must be "gnm".
		 */
		if (sheet->sheet_type == GNM_SHEET_OBJECT)
			gsf_xml_out_add_cstr (state->output, GNM "SheetType", "object");
		gsf_xml_out_add_int (state->output, GNM "Cols",
				     gnm_sheet_get_max_cols (sheet));
		gsf_xml_out_add_int (state->output, GNM "Rows",
				     gnm_sheet_get_max_rows (sheet));
		gsf_xml_out_add_cstr (state->output, NULL, sheet->name_unquoted);
		gsf_xml_out_end_element (state->output); /* </gnm:SheetName> */
	}
	gsf_xml_out_end_element (state->output); /* </gnm:SheetNameIndex> */
}

static void
xml_write_name (GnmOutputXML *state, GnmNamedExpr *nexpr)
{
	char *expr_str;

	g_return_if_fail (nexpr != NULL);

	gsf_xml_out_start_element (state->output, GNM "Name");
	gsf_xml_out_simple_element (state->output, GNM "name",
				    expr_name_name (nexpr));
	expr_str = expr_name_as_string (nexpr, NULL, state->convs);
	gsf_xml_out_simple_element (state->output, GNM "value", expr_str);
	g_free (expr_str);
	gsf_xml_out_simple_element (state->output, GNM "position",
		cellpos_as_string (&nexpr->pos.eval));
	gsf_xml_out_end_element (state->output); /* </gnm:Name> */
}

static void
xml_write_named_expressions (GnmOutputXML *state, GnmNamedExprCollection *scope)
{
	GSList *names =
		g_slist_sort (gnm_named_expr_collection_list (scope),
			      (GCompareFunc)expr_name_cmp_by_name);
	GSList *p;

	if (!names)
		return;

	gsf_xml_out_start_element (state->output, GNM "Names");
	for (p = names; p; p = p->next) {
		GnmNamedExpr *nexpr = p->data;
		xml_write_name (state, nexpr);
	}
	gsf_xml_out_end_element (state->output); /* </gnm:Names> */
	g_slist_free (names);
}

static void
xml_write_geometry (GnmOutputXML *state)
{
	if (state->wb_view->preferred_width > 0 ||
	    state->wb_view->preferred_height > 0) {
		gsf_xml_out_start_element (state->output, GNM "Geometry");
		gsf_xml_out_add_int (state->output, "Width", state->wb_view->preferred_width);
		gsf_xml_out_add_int (state->output, "Height", state->wb_view->preferred_height);
		gsf_xml_out_end_element (state->output); /* </gnm:Geometry> */
	}
}

static void
xml_write_print_unit (GnmOutputXML *state, char const *name,
		      double points, GtkUnit unit)
{
	gsf_xml_out_start_element (state->output, name);
	xml_out_add_points (state->output, "Points", points);
	gsf_xml_out_add_cstr_unchecked (state->output, "PrefUnit",
					unit_to_unit_name (unit));
	gsf_xml_out_end_element (state->output);
}

static void
xml_write_print_repeat_range (GnmOutputXML *state,
			      char const *name,
			      const char *range)
{
	if (range && *range) {
		gsf_xml_out_start_element (state->output, name);
		gsf_xml_out_add_cstr_unchecked (state->output, "value", range);
		gsf_xml_out_end_element (state->output);
	}
}

static void
xml_write_print_hf (GnmOutputXML *state, char const *name,
		    GnmPrintHF const *hf)
{
	gsf_xml_out_start_element (state->output, name);
	gsf_xml_out_add_cstr (state->output, "Left", hf->left_format);
	gsf_xml_out_add_cstr (state->output, "Middle", hf->middle_format);
	gsf_xml_out_add_cstr (state->output, "Right", hf->right_format);
	gsf_xml_out_end_element (state->output);

}

static void
xml_write_breaks (GnmOutputXML *state, GnmPageBreaks *breaks)
{
	GArray const *details = breaks->details;
	GnmPageBreak const *binfo;
	unsigned i;

	gsf_xml_out_start_element (state->output,
		(breaks->is_vert) ? GNM "vPageBreaks" : GNM "hPageBreaks");
	gsf_xml_out_add_int (state->output, "count", details->len);

	for (i = 0 ; i < details->len ; i++) {
		binfo = &g_array_index (details, GnmPageBreak, i);
		gsf_xml_out_start_element (state->output, GNM "break");
		gsf_xml_out_add_int (state->output, "pos", binfo->pos);
		if (binfo->type == GNM_PAGE_BREAK_MANUAL)
			gsf_xml_out_add_cstr_unchecked  (state->output, "type", "manual");
		else if (binfo->type == GNM_PAGE_BREAK_DATA_SLICE)
			gsf_xml_out_add_cstr_unchecked  (state->output, "type", "data-slice");
		else if (binfo->type == GNM_PAGE_BREAK_AUTO)
			gsf_xml_out_add_cstr_unchecked  (state->output, "type", "auto");
		gsf_xml_out_end_element (state->output); /* </break> */
	}
	gsf_xml_out_end_element (state->output);
}

static void
xml_write_print_info (GnmOutputXML *state, GnmPrintInformation *pi)
{
	char  *paper_name;
	char const *uri;
	double header;
	double footer;
	double left;
	double right;
	double edge_to_above_footer;
	double edge_to_below_header;
	GtkPageOrientation orient;

	g_return_if_fail (pi != NULL);

	gsf_xml_out_start_element (state->output, GNM "PrintInformation");

	gsf_xml_out_start_element (state->output, GNM "Margins");

	print_info_get_margins (pi, &header, &footer, &left, &right,
				&edge_to_below_header, &edge_to_above_footer);
	xml_write_print_unit (state, GNM "top", edge_to_below_header,
			      pi->desired_display.header);
	xml_write_print_unit (state, GNM "bottom", edge_to_above_footer,
			      pi->desired_display.footer);
	xml_write_print_unit (state, GNM "left", left,
			      pi->desired_display.left);
	xml_write_print_unit (state, GNM "right", right,
			      pi->desired_display.right);
	xml_write_print_unit (state, GNM "header", header,
			      pi->desired_display.top);
	xml_write_print_unit (state, GNM "footer", footer,
			      pi->desired_display.bottom);
	gsf_xml_out_end_element (state->output);

	gsf_xml_out_start_element (state->output, GNM "Scale");
	if (pi->scaling.type == PRINT_SCALE_PERCENTAGE) {
		gsf_xml_out_add_cstr_unchecked  (state->output, "type", "percentage");
		go_xml_out_add_double  (state->output, "percentage", pi->scaling.percentage.x);
	} else {
		gsf_xml_out_add_cstr_unchecked  (state->output, "type", "size_fit");
		go_xml_out_add_double  (state->output, "cols", pi->scaling.dim.cols);
		go_xml_out_add_double  (state->output, "rows", pi->scaling.dim.rows);
	}
	gsf_xml_out_end_element (state->output);

	gsf_xml_out_start_element (state->output, GNM "vcenter");
	gsf_xml_out_add_int  (state->output, "value", pi->center_vertically);
	gsf_xml_out_end_element (state->output);

	gsf_xml_out_start_element (state->output, GNM "hcenter");
	gsf_xml_out_add_int  (state->output, "value", pi->center_horizontally);
	gsf_xml_out_end_element (state->output);

	gsf_xml_out_start_element (state->output, GNM "grid");
	gsf_xml_out_add_int  (state->output, "value",    pi->print_grid_lines);
	gsf_xml_out_end_element (state->output);

	gsf_xml_out_start_element (state->output, GNM "even_if_only_styles");
	gsf_xml_out_add_int  (state->output, "value",    pi->print_even_if_only_styles);
	gsf_xml_out_end_element (state->output);

	gsf_xml_out_start_element (state->output, GNM "monochrome");
	gsf_xml_out_add_int  (state->output, "value",    pi->print_black_and_white);
	gsf_xml_out_end_element (state->output);

	gsf_xml_out_start_element (state->output, GNM "draft");
	gsf_xml_out_add_int  (state->output, "value",    pi->print_as_draft);
	gsf_xml_out_end_element (state->output);

	gsf_xml_out_start_element (state->output, GNM "titles");
	gsf_xml_out_add_int  (state->output, "value",    pi->print_titles);
	gsf_xml_out_end_element (state->output);

	gsf_xml_out_start_element (state->output, GNM "do_not_print");
	gsf_xml_out_add_int  (state->output, "value",    pi->do_not_print);
	gsf_xml_out_end_element (state->output);

	gsf_xml_out_start_element (state->output, GNM "print_range");
	gsf_xml_out_add_enum (state->output, "value",
			      GNM_PRINT_RANGE_TYPE,
			      print_info_get_printrange (pi) );
	gsf_xml_out_end_element (state->output);

	xml_write_print_repeat_range (state, GNM "repeat_top", pi->repeat_top);
	xml_write_print_repeat_range (state, GNM "repeat_left", pi->repeat_left);

	/* this was once an enum, hence the silly strings */
	gsf_xml_out_simple_element (state->output, GNM "order",
		pi->print_across_then_down ? "r_then_d" :"d_then_r");

	orient = print_info_get_paper_orientation (pi);
	gsf_xml_out_simple_element (state->output, GNM "orientation",
		(orient == GTK_PAGE_ORIENTATION_PORTRAIT
		 || orient == GTK_PAGE_ORIENTATION_REVERSE_PORTRAIT)
				    ? "portrait" : "landscape");
#warning TODO: we should also handle inversion

	xml_write_print_hf (state, GNM "Header", pi->header);
	xml_write_print_hf (state, GNM "Footer", pi->footer);

	paper_name = print_info_get_paper (pi);
	if (paper_name)
		gsf_xml_out_simple_element (state->output, GNM "paper",
					    paper_name);
	g_free (paper_name);

	uri = print_info_get_printtofile_uri (pi);
	if (uri)
		gsf_xml_out_simple_element (state->output, GNM "print-to-uri",
					    uri);

	if (NULL != pi->page_breaks.v)
		xml_write_breaks (state, pi->page_breaks.v);
	if (NULL != pi->page_breaks.h)
		xml_write_breaks (state, pi->page_breaks.h);

	gsf_xml_out_start_element (state->output, GNM "comments");
	gsf_xml_out_add_enum (state->output, "placement",
			      GNM_PRINT_COMMENT_PLACEMENT_TYPE,
			      pi->comment_placement);
	gsf_xml_out_end_element (state->output);

	gsf_xml_out_start_element (state->output, GNM "errors");
	gsf_xml_out_add_enum (state->output, "PrintErrorsAs",
			      GNM_PRINT_ERRORS_TYPE,
			      pi->error_display);
	gsf_xml_out_end_element (state->output);

	gsf_xml_out_end_element (state->output);
}

static void
xml_write_style (GnmOutputXML *state, GnmStyle const *style)
{
	static char const *border_names[] = {
		GNM "Top",
		GNM "Bottom",
		GNM "Left",
		GNM "Right",
		GNM "Rev-Diagonal",
		GNM "Diagonal"
	};
	GnmValidation const *v;
	GnmHLink   const *lnk;
	GnmInputMsg const *im;
	GnmStyleConditions const *sc;
	GnmStyleBorderType t;
	unsigned i;
	gboolean started;

	gsf_xml_out_start_element (state->output, GNM "Style");

	if (gnm_style_is_element_set (style, MSTYLE_ALIGN_H))
		gsf_xml_out_add_enum (state->output, "HAlign",
				      GNM_ALIGN_H_TYPE,
				      gnm_style_get_align_h (style));
	if (gnm_style_is_element_set (style, MSTYLE_ALIGN_V))
		gsf_xml_out_add_enum (state->output, "VAlign",
				      GNM_ALIGN_V_TYPE,
				      gnm_style_get_align_v (style));
	if (gnm_style_is_element_set (style, MSTYLE_WRAP_TEXT))
		gsf_xml_out_add_bool (state->output, "WrapText",
				      gnm_style_get_wrap_text (style));
	if (gnm_style_is_element_set (style, MSTYLE_SHRINK_TO_FIT))
		gsf_xml_out_add_bool (state->output, "ShrinkToFit",
				      gnm_style_get_shrink_to_fit (style));
	if (gnm_style_is_element_set (style, MSTYLE_ROTATION))
		gsf_xml_out_add_int (state->output, "Rotation",
				     gnm_style_get_rotation (style));
	if (gnm_style_is_element_set (style, MSTYLE_PATTERN))
		gsf_xml_out_add_int (state->output, "Shade",
				     gnm_style_get_pattern (style));
	if (gnm_style_is_element_set (style, MSTYLE_INDENT))
		gsf_xml_out_add_int (state->output, "Indent", gnm_style_get_indent (style));
	if (gnm_style_is_element_set (style, MSTYLE_CONTENTS_LOCKED))
		gsf_xml_out_add_bool (state->output, "Locked",
				      gnm_style_get_contents_locked (style));
	if (gnm_style_is_element_set (style, MSTYLE_CONTENTS_HIDDEN))
		gsf_xml_out_add_bool (state->output, "Hidden",
				      gnm_style_get_contents_hidden (style));
	if (gnm_style_is_element_set (style, MSTYLE_FONT_COLOR))
		gnm_xml_out_add_color (state->output, "Fore",
				       gnm_style_get_font_color (style));
	if (gnm_style_is_element_set (style, MSTYLE_COLOR_BACK))
		gnm_xml_out_add_color (state->output, "Back",
				       gnm_style_get_back_color (style));
	if (gnm_style_is_element_set (style, MSTYLE_COLOR_PATTERN))
		gnm_xml_out_add_color (state->output, "PatternColor",
				       gnm_style_get_pattern_color (style));
	if (gnm_style_is_element_set (style, MSTYLE_FORMAT)) {
		const char *fmt = go_format_as_XL (gnm_style_get_format (style));
		gsf_xml_out_add_cstr (state->output, "Format", fmt);
	}

	if (gnm_style_is_element_set (style, MSTYLE_FONT_NAME) ||
	    gnm_style_is_element_set (style, MSTYLE_FONT_SIZE) ||
	    gnm_style_is_element_set (style, MSTYLE_FONT_BOLD) ||
	    gnm_style_is_element_set (style, MSTYLE_FONT_ITALIC) ||
	    gnm_style_is_element_set (style, MSTYLE_FONT_UNDERLINE) ||
	    gnm_style_is_element_set (style, MSTYLE_FONT_STRIKETHROUGH) ||
	    gnm_style_is_element_set (style, MSTYLE_FONT_SCRIPT)) {
		gsf_xml_out_start_element (state->output, GNM "Font");

		if (gnm_style_is_element_set (style, MSTYLE_FONT_SIZE))
			xml_out_add_points (state->output, "Unit", gnm_style_get_font_size (style));
		if (gnm_style_is_element_set (style, MSTYLE_FONT_BOLD))
			gsf_xml_out_add_int (state->output, "Bold", gnm_style_get_font_bold (style));
		if (gnm_style_is_element_set (style, MSTYLE_FONT_ITALIC))
			gsf_xml_out_add_int (state->output, "Italic", gnm_style_get_font_italic (style));
		if (gnm_style_is_element_set (style, MSTYLE_FONT_UNDERLINE))
			gsf_xml_out_add_int (state->output, "Underline", (int)gnm_style_get_font_uline (style));
		if (gnm_style_is_element_set (style, MSTYLE_FONT_STRIKETHROUGH))
			gsf_xml_out_add_int (state->output, "StrikeThrough", gnm_style_get_font_strike (style));
		if (gnm_style_is_element_set (style, MSTYLE_FONT_SCRIPT))
			gsf_xml_out_add_int (state->output, "Script", (int)gnm_style_get_font_script (style));

		if (gnm_style_is_element_set (style, MSTYLE_FONT_NAME)) {
			const char *fontname = gnm_style_get_font_name (style);
			gsf_xml_out_add_cstr (state->output, NULL, fontname);
		}

		gsf_xml_out_end_element (state->output);
	}

	if (gnm_style_is_element_set (style, MSTYLE_HLINK) &&
	    NULL != (lnk = gnm_style_get_hlink (style))) {
		gsf_xml_out_start_element (state->output, GNM "HyperLink");
		gsf_xml_out_add_cstr (state->output, "type", g_type_name (G_OBJECT_TYPE (lnk)));
		gsf_xml_out_add_cstr (state->output, "target", gnm_hlink_get_target (lnk));
		if (gnm_hlink_get_tip (lnk) != NULL)
			gsf_xml_out_add_cstr (state->output, "tip", gnm_hlink_get_tip (lnk));
		gsf_xml_out_end_element (state->output);
	}

	if (gnm_style_is_element_set (style, MSTYLE_VALIDATION) &&
	    NULL != (v = gnm_style_get_validation (style))) {
		unsigned ui;

		gsf_xml_out_start_element (state->output, GNM "Validation");
		gsf_xml_out_add_enum (state->output, "Style",
				      GNM_VALIDATION_STYLE_TYPE, v->style);
		gsf_xml_out_add_enum (state->output, "Type",
				      GNM_VALIDATION_TYPE_TYPE, v->type);

		switch (v->type) {
		case GNM_VALIDATION_TYPE_AS_INT:
		case GNM_VALIDATION_TYPE_AS_NUMBER:
		case GNM_VALIDATION_TYPE_AS_DATE:
		case GNM_VALIDATION_TYPE_AS_TIME:
		case GNM_VALIDATION_TYPE_TEXT_LENGTH:
			gsf_xml_out_add_enum (state->output, "Operator",
					      GNM_VALIDATION_OP_TYPE, v->op);
			break;
		default:
			break;
		}

		gsf_xml_out_add_bool (state->output, "AllowBlank", v->allow_blank);
		gsf_xml_out_add_bool (state->output, "UseDropdown", v->use_dropdown);

		if (v->title != NULL && v->title->str[0] != '\0')
			gsf_xml_out_add_cstr (state->output, "Title", v->title->str);
		if (v->msg != NULL && v->msg->str[0] != '\0')
			gsf_xml_out_add_cstr (state->output, "Message", v->msg->str);

		for (ui = 0; ui < G_N_ELEMENTS (v->deps); ui++) {
			GnmExprTop const *texpr = dependent_managed_get_expr (&v->deps[ui]);
			if (texpr) {
				const char *elem = ui == 0 ? GNM "Expression0" : GNM "Expression1";
				char *tmp;
				GnmParsePos pp;
				parse_pos_init_sheet (&pp, (Sheet *)state->sheet);
				tmp = gnm_expr_top_as_string (texpr, &pp, state->convs);
				gsf_xml_out_simple_element (state->output, elem, tmp);
				g_free (tmp);
			}
		}
		gsf_xml_out_end_element (state->output); /* </Validation> */
	}

	if (gnm_style_is_element_set (style, MSTYLE_INPUT_MSG) &&
	    NULL != (im = gnm_style_get_input_msg (style))) {
		char const *txt;
		gsf_xml_out_start_element (state->output, GNM "InputMessage");
		if (NULL != (txt = gnm_input_msg_get_title (im)))
			gsf_xml_out_add_cstr (state->output, "Title", txt);
		if (NULL != (txt = gnm_input_msg_get_msg (im)))
			gsf_xml_out_add_cstr (state->output, "Message", txt);
		gsf_xml_out_end_element (state->output); /* </InputMessage> */
	}

	if (gnm_style_is_element_set (style, MSTYLE_CONDITIONS) &&
	    NULL != (sc = gnm_style_get_conditions (style))) {
		GPtrArray const *conds = gnm_style_conditions_details (sc);
		if (conds != NULL) {
			GnmParsePos pp;
			parse_pos_init_sheet (&pp, (Sheet *)state->sheet);

			for (i = 0 ; i < conds->len ; i++) {
				unsigned ui;
				GnmStyleCond const *cond =
					g_ptr_array_index (conds, i);
				gsf_xml_out_start_element (state->output, GNM "Condition");
				gsf_xml_out_add_int (state->output, "Operator", cond->op);
				for (ui = 0; ui < 2; ui++) {
					GnmExprTop const *texpr = gnm_style_cond_get_expr (cond, ui);
					char *tmp = texpr
						? gnm_expr_top_as_string (texpr, &pp, state->convs)
						: NULL;
					const char *attr = (ui == 0)
						? GNM "Expression0"
						: GNM "Expression1";
					if (tmp) {
						gsf_xml_out_simple_element (state->output, attr, tmp);
						g_free (tmp);
					}
				}
				xml_write_style (state, cond->overlay);
				gsf_xml_out_end_element (state->output); /* </Condition> */
			}
		}
	}

	started = FALSE;
	for (i = MSTYLE_BORDER_TOP; i <= MSTYLE_BORDER_DIAGONAL; i++) {
		GnmBorder const *border;
		if (gnm_style_is_element_set (style, i) &&
		    NULL != (border = gnm_style_get_border (style, i)) &&
		    GNM_STYLE_BORDER_NONE != (t = border->line_type)) {
			GnmColor const *col   = border->color;

			if (!started) {
				gsf_xml_out_start_element (state->output, GNM "StyleBorder");
				started = TRUE;
			}

			gsf_xml_out_start_element (state->output,
				border_names [i - MSTYLE_BORDER_TOP]);
			gsf_xml_out_add_int (state->output, "Style", t);
			gnm_xml_out_add_color (state->output, "Color", col);
			gsf_xml_out_end_element (state->output);
		}
	}
	if (started)
		gsf_xml_out_end_element (state->output);

	gsf_xml_out_end_element (state->output);
}

static void
xml_write_style_region (GnmOutputXML *state, GnmStyleRegion const *region)
{
	gsf_xml_out_start_element (state->output, GNM "StyleRegion");
	xml_out_add_range (state->output, &region->range);
	if (region->style != NULL)
		xml_write_style (state, region->style);
	gsf_xml_out_end_element (state->output);
}

static int
cb_sheet_style_order (GnmStyleRegion const *a, GnmStyleRegion const *b)
{
	GnmRange const *ra = &a->range;
	GnmRange const *rb = &b->range;
	int res;

	res = ra->start.col - rb->start.col;

	if (res == 0)
		res = ra->start.row - rb->start.row;

	return res;
}

static void
xml_write_styles (GnmOutputXML *state)
{
	GnmStyleList *styles =
		g_slist_sort (sheet_style_get_range (state->sheet, NULL),
			      (GCompareFunc)cb_sheet_style_order);
	if (styles != NULL) {
		GnmStyleList *ptr;

		gsf_xml_out_start_element (state->output, GNM "Styles");
		for (ptr = styles; ptr; ptr = ptr->next)
			xml_write_style_region (state, ptr->data);
		gsf_xml_out_end_element (state->output);
		style_list_free (styles);
	}
}

typedef struct {
	GnmOutputXML *state;
	gboolean is_column;
	ColRowInfo prev;
	int prev_pos, rle_count;
	GnmCellRegion const *cr;
} closure_write_colrow;

static gboolean
xml_write_colrow_info (GnmColRowIter const *iter, closure_write_colrow *closure)
{
	ColRowInfo const *prev = &closure->prev;
	GsfXMLOut *output = closure->state->output;
	ColRowInfo const *def =
		sheet_colrow_get_default (closure->state->sheet,
					  closure->is_column);

	closure->rle_count++;
	if (NULL != iter &&
	    iter->pos == closure->prev_pos + closure->rle_count &&
	    col_row_info_equal (prev, iter->cri))
		return FALSE;

	if (closure->prev_pos != -1 && !col_row_info_equal (prev, def)) {
		if (closure->is_column)
			gsf_xml_out_start_element (output, GNM "ColInfo");
		else
			gsf_xml_out_start_element (output, GNM "RowInfo");

		gsf_xml_out_add_int (output, "No", closure->prev_pos);
		xml_out_add_points (output, "Unit", prev->size_pts);
		if (prev->hard_size)
			gsf_xml_out_add_bool (output, "HardSize", TRUE);
		if (!prev->visible)
			gsf_xml_out_add_bool (output, "Hidden", TRUE);
		if (prev->is_collapsed)
			gsf_xml_out_add_bool (output, "Collapsed", TRUE);
		if (prev->outline_level > 0)
			gsf_xml_out_add_int (output, "OutlineLevel", prev->outline_level);

		if (closure->rle_count > 1)
			gsf_xml_out_add_int (output, "Count", closure->rle_count);
		gsf_xml_out_end_element (output);
	}

	closure->rle_count = 0;
	if (NULL != iter) {
		closure->prev = *iter->cri;
		closure->prev_pos = iter->pos;
	}

	return FALSE;
}

static void
xml_write_cols_rows (GnmOutputXML *state, GnmCellRegion const *cr)
{
	const Sheet *sheet = state->sheet;
	int i;

	for (i = 0; i < 2; i++) {
		closure_write_colrow closure;
		gboolean is_cols = (i == 0);

		gsf_xml_out_start_element (state->output,
					   is_cols ? GNM "Cols" : GNM "Rows");

		if (sheet)
			xml_out_add_points
				(state->output, "DefaultSizePts",
				 sheet_colrow_get_default (sheet, is_cols)->size_pts);

		closure.state = state;
		closure.cr = cr;
		closure.is_column = is_cols;
		memset (&closure.prev, 0, sizeof (closure.prev));
		closure.prev_pos = -1;
		closure.rle_count = 0;
		if (cr)
			colrow_state_list_foreach
				(is_cols ? cr->col_state : cr->row_state,
				 sheet, is_cols,
				 is_cols ? cr->base.col : cr->base.row,
				 (ColRowHandler)&xml_write_colrow_info,
				 &closure);
		else
			sheet_colrow_foreach
				(sheet, is_cols, 0, -1,
				 (ColRowHandler)&xml_write_colrow_info,
				 &closure);
		xml_write_colrow_info (NULL, &closure); /* flush */
		gsf_xml_out_end_element (state->output); /* </gnm:Cols> */
	}
}

static void
xml_write_selection_info (GnmOutputXML *state)
{
	GSList *ptr, *copy;
	SheetView const *sv = sheet_get_view (state->sheet, state->wb_view);
	if (!sv) return;  /* Hidden.  */

	gsf_xml_out_start_element (state->output, GNM "Selections");
	gsf_xml_out_add_int (state->output, "CursorCol", sv->edit_pos_real.col);
	gsf_xml_out_add_int (state->output, "CursorRow", sv->edit_pos_real.row);

	/* Insert the selections in REVERSE order */
	copy = g_slist_reverse (g_slist_copy (sv->selections));
	for (ptr = copy; ptr; ptr = ptr->next) {
		GnmRange const *r = ptr->data;
		gsf_xml_out_start_element (state->output, GNM "Selection");
		xml_out_add_range (state->output, r);
		gsf_xml_out_end_element (state->output); /* </gnm:Selection> */
	}
	g_slist_free (copy);

	gsf_xml_out_end_element (state->output); /* </gnm:Selections> */
}

static void
xml_write_cell_and_position (GnmOutputXML *state,
			     GnmExprTop const *texpr, GnmValue const *val,
			     GnmParsePos const *pp)
{
	gboolean write_contents = TRUE;
	gboolean const is_shared_expr = (texpr != NULL) &&
		gnm_expr_top_is_shared (texpr);

	/* Only the top left corner of an array needs to be saved (>= 0.53) */
	if (texpr && gnm_expr_top_is_array_elem (texpr, NULL, NULL))
		return; /* DOM version would write <Cell Col= Row=/> */

	gsf_xml_out_start_element (state->output, GNM "Cell");
	gsf_xml_out_add_int (state->output, "Row", pp->eval.row);
	gsf_xml_out_add_int (state->output, "Col", pp->eval.col);

	/* As of version 0.53 we save the ID of shared expressions */
	if (is_shared_expr) {
		gpointer id = g_hash_table_lookup (state->expr_map, (gpointer) texpr);

		if (id == NULL) {
			id = GINT_TO_POINTER (g_hash_table_size (state->expr_map) + 1);
			g_hash_table_insert (state->expr_map, (gpointer)texpr, id);
		} else
			write_contents = FALSE;

		gsf_xml_out_add_int (state->output, "ExprID", GPOINTER_TO_INT (id));
	}

	/* As of version 0.53 we save the size of the array as attributes */
	/* As of version 0.57 the attributes are in the Cell not the Content */
	if (texpr && gnm_expr_top_is_array_corner (texpr)) {
		int cols, rows;
		gnm_expr_top_get_array_size (texpr, &cols, &rows);
		gsf_xml_out_add_int (state->output, "Rows", rows);
		gsf_xml_out_add_int (state->output, "Cols", cols);
	}

	if (write_contents) {
		gboolean write_value = !texpr || state->write_value_result;
		GString *str = state->cell_str;

		g_string_truncate (str, 0);

		if (write_value) {
			if (val != NULL) {
				gsf_xml_out_add_int (state->output, "ValueType", val->v_any.type);
				if (VALUE_FMT (val) != NULL) {
					const char *fmt = go_format_as_XL (VALUE_FMT (val));
					gsf_xml_out_add_cstr (state->output, "ValueFormat", fmt);
				}
				value_get_as_gstring (val, str, state->convs);
				if (texpr) {
					gsf_xml_out_add_cstr (state->output, "Value", str->str);
					g_string_truncate (str, 0);
				}
			} else {
				g_warning ("%s has no value ?", cellpos_as_string (&pp->eval));
			}
		}

		if (texpr) {
			GnmConventionsOut out;
			out.accum = str;
			out.pp    = pp;
			out.convs = state->convs;

			g_string_append_c (str, '=');
			gnm_expr_top_as_gstring (texpr, &out);
		}

		gsf_xml_out_add_cstr (state->output, NULL, str->str);
	}
	gsf_xml_out_end_element (state->output); /* </gnm:Cell> */
}

static GnmValue *
cb_write_cell (GnmCellIter const *iter, GnmOutputXML *state)
{
	GnmExprTop const *texpr = iter->cell->base.texpr;
	GnmValue const *value = iter->cell->value;

	if (texpr == NULL && VALUE_IS_EMPTY (value))
		return NULL;

	xml_write_cell_and_position (state, texpr, value, &iter->pp);
	return NULL;
}

static void
xml_write_cells (GnmOutputXML *state)
{
	gsf_xml_out_start_element (state->output, GNM "Cells");
	sheet_foreach_cell_in_region ((Sheet *)state->sheet,
				      CELL_ITER_IGNORE_NONEXISTENT,
				      0, 0, -1, -1,
				      (CellIterFunc) cb_write_cell, state);
	gsf_xml_out_end_element (state->output); /* </gnm:Cells> */
}

static void
xml_write_merged_regions (GnmOutputXML *state)
{
	GSList *ptr = state->sheet->list_merged;
	if (ptr == NULL)
		return;
	gsf_xml_out_start_element (state->output, GNM "MergedRegions");
	for (; ptr != NULL ; ptr = ptr->next)
		gsf_xml_out_simple_element (state->output,
			GNM "Merge", range_as_string (ptr->data));
	gsf_xml_out_end_element (state->output); /* </gnm:MergedRegions> */
}

static void
xml_write_sheet_layout (GnmOutputXML *state)
{
	SheetView const *sv = sheet_get_view (state->sheet, state->wb_view);
	if (!sv) return;  /* Hidden.  */

	gsf_xml_out_start_element (state->output, GNM "SheetLayout");
	gnm_xml_out_add_cellpos (state->output, "TopLeft", &sv->initial_top_left);

	if (gnm_sheet_view_is_frozen (sv)) {
		gsf_xml_out_start_element (state->output, GNM "FreezePanes");
		gnm_xml_out_add_cellpos (state->output, "FrozenTopLeft", &sv->frozen_top_left);
		gnm_xml_out_add_cellpos (state->output, "UnfrozenTopLeft", &sv->unfrozen_top_left);
		gsf_xml_out_end_element (state->output); /* </gnm:FreezePanes> */
	}
	gsf_xml_out_end_element (state->output); /* </gnm:SheetLayout> */
}

static void
xml_write_filter_expr (GnmOutputXML *state,
		       GnmFilterCondition const *cond, unsigned i)
{
	static char const *filter_cond_name[] = { "eq", "gt", "lt", "gte", "lte", "ne" };
	/*
	 * WARNING WARNING WARING
	 * Value and ValueType are _reversed !!!
	 */
	static struct { char const *op, *valtype, *val; } filter_expr_attrs[] = {
		{ "Op0", "Value0", "ValueType0" },
		{ "Op1", "Value1", "ValueType1" }
	};

	GString *text = g_string_new (NULL);
	value_get_as_gstring (cond->value[i], text, state->convs);
	gsf_xml_out_add_cstr_unchecked (state->output,
		filter_expr_attrs[i].op, filter_cond_name [cond->op[i]]);
	gsf_xml_out_add_int (state->output,
		filter_expr_attrs[i].valtype, cond->value[i]->v_any.type);
	gsf_xml_out_add_cstr (state->output,
		filter_expr_attrs[i].val, text->str);
	g_string_free (text, TRUE);
}

static void
xml_write_filter_field (GnmOutputXML *state,
			GnmFilterCondition const *cond, unsigned i)
{
	gsf_xml_out_start_element (state->output, GNM "Field");
	gsf_xml_out_add_int (state->output, "Index", i);

	switch (GNM_FILTER_OP_TYPE_MASK & cond->op[0]) {
	case 0: gsf_xml_out_add_cstr_unchecked (state->output, "Type", "expr");
		xml_write_filter_expr (state, cond, 0);
		if (cond->op[1] != GNM_FILTER_UNUSED) {
			xml_write_filter_expr (state, cond, 1);
			gsf_xml_out_add_bool (state->output, "IsAnd", cond->is_and);
		}
		break;
	case GNM_FILTER_OP_BLANKS:
		gsf_xml_out_add_cstr_unchecked (state->output, "Type", "blanks");
		break;
	case GNM_FILTER_OP_NON_BLANKS:
		gsf_xml_out_add_cstr_unchecked (state->output, "Type", "nonblanks");
		break;
	case GNM_FILTER_OP_TOP_N:
		gsf_xml_out_add_cstr_unchecked (state->output, "Type", "bucket");
		gsf_xml_out_add_bool (state->output, "top",
				      !(cond->op[0] & GNM_FILTER_OP_BOTTOM_MASK));
		gsf_xml_out_add_bool (state->output, "items",
				      !(cond->op[0] & GNM_FILTER_OP_PERCENT_MASK));
		gsf_xml_out_add_bool (state->output, "rel_range",
				      !(cond->op[0] & GNM_FILTER_OP_REL_N_MASK));
		go_xml_out_add_double (state->output, "count", cond->count);
		break;
	}

	gsf_xml_out_end_element (state->output); /* </gnm:Field> */
}

static void
xml_write_sheet_filters (GnmOutputXML *state)
{
	GSList *ptr;
	GnmFilter *filter;
	GnmFilterCondition const *cond;
	unsigned i;

	if (state->sheet->filters == NULL)
		return;

	gsf_xml_out_start_element (state->output, GNM "Filters");

	for (ptr = state->sheet->filters; ptr != NULL ; ptr = ptr->next) {
		filter = ptr->data;
		gsf_xml_out_start_element (state->output, GNM "Filter");
		gsf_xml_out_add_cstr_unchecked (state->output, "Area",
			range_as_string (&filter->r));

		for (i = filter->fields->len ; i-- > 0 ; ) {
			cond = gnm_filter_get_condition (filter, i);
			if (cond != NULL && cond->op[0] != GNM_FILTER_UNUSED)
				xml_write_filter_field (state, cond, i);
		}

		gsf_xml_out_end_element (state->output); /* </gnm:Filter> */
	}

	gsf_xml_out_end_element (state->output); /* </gnm:Filters> */
}

static void
xml_write_solver (GnmOutputXML *state)
{
        GnmSolverParameters *param = state->sheet->solver_parameters;
	GSList *ptr;
	GnmCellRef const *target;
	GnmValue const *input;

	if (param == NULL)
		return;

	gsf_xml_out_start_element (state->output, GNM "Solver");

	target = gnm_solver_param_get_target (param);
	if (target != NULL) {
		GnmExpr const *expr = gnm_expr_new_cellref (target);
		GnmParsePos pp;
		char *txt = gnm_expr_as_string
			(expr,
			 parse_pos_init_sheet (&pp, state->sheet),
			 state->convs);
		gsf_xml_out_add_cstr (state->output, "Target", txt);
		g_free (txt);
		gnm_expr_free (expr);
	}

	gsf_xml_out_add_int (state->output, "ModelType", param->options.model_type);
	gsf_xml_out_add_int (state->output, "ProblemType", param->problem_type);
	input = gnm_solver_param_get_input (param);
	if (input)
		gsf_xml_out_add_cstr (state->output, "Inputs",
				      value_peek_string (input));
	gsf_xml_out_add_int (state->output, "MaxTime",
		param->options.max_time_sec);
	gsf_xml_out_add_int (state->output, "MaxIter",
		param->options.max_iter);
	gsf_xml_out_add_bool (state->output, "NonNeg",
		param->options.assume_non_negative);
	gsf_xml_out_add_bool (state->output, "Discr",
		param->options.assume_discrete);
	gsf_xml_out_add_bool (state->output, "AutoScale",
		param->options.automatic_scaling);
	gsf_xml_out_add_bool (state->output, "ProgramR",
		param->options.program_report);
	gsf_xml_out_add_bool (state->output, "SensitivityR",
		param->options.sensitivity_report);

	for (ptr = param->constraints; ptr != NULL ; ptr = ptr->next) {
		GnmSolverConstraint const *c = ptr->data;
		int type;
		GString *str = g_string_new (NULL);

		/* Historical values.  Not a bit field.  */
		switch (c->type) {
		default:	 type = 0;	break;
		case GNM_SOLVER_LE:   type = 1;	break;
		case GNM_SOLVER_GE:   type = 2;	break;
		case GNM_SOLVER_EQ:   type = 4;	break;
		case GNM_SOLVER_INTEGER:  type = 8;	break;
		case GNM_SOLVER_BOOLEAN: type = 16;	break;
		}

		gsf_xml_out_start_element (state->output, GNM "Constr");
		gsf_xml_out_add_int (state->output, "Type", type);

		gnm_solver_constraint_side_as_str (c, state->sheet, str, TRUE);
		gsf_xml_out_add_cstr (state->output, "lhs", str->str);

		if (gnm_solver_constraint_has_rhs (c)) {
			g_string_truncate (str, 0);
			gnm_solver_constraint_side_as_str (c, state->sheet,
							   str, FALSE);
			gsf_xml_out_add_cstr (state->output, "rhs", str->str);
		}

		gsf_xml_out_end_element (state->output); /* </gnm:Constr> */

		g_string_free (str, TRUE);
	}

	gsf_xml_out_end_element (state->output); /* </gnm:Solver> */
}

static void
xml_write_scenario (GnmOutputXML *state, GnmScenario const *sc)
{
	GSList *l;
	GnmParsePos pp;

	parse_pos_init_sheet (&pp, sc->sheet);

	gsf_xml_out_start_element (state->output, GNM "Scenario");

	gsf_xml_out_add_cstr (state->output, "Name", sc->name);
	if (sc->comment)
		gsf_xml_out_add_cstr (state->output, "Comment", sc->comment);

	for (l = sc->items; l; l = l->next) {
		GnmScenarioItem const *sci = l->data;
		GnmValue const *val = sci->value;
		GString *str;
		GnmConventionsOut out;

		if (!gnm_scenario_item_valid (sci, NULL))
			continue;

		str = g_string_new (NULL);
		gsf_xml_out_start_element (state->output, GNM "Item");

		out.accum = str;
		out.pp    = &pp;
		out.convs = state->convs;

		gnm_expr_top_as_gstring (dependent_managed_get_expr (&sci->dep), &out);
		gsf_xml_out_add_cstr (state->output, "Range", str->str);

		if (val) {
			gsf_xml_out_add_int (state->output,
					     "ValueType",
					     val->v_any.type);
			if (VALUE_FMT (val) != NULL) {
				const char *fmt = go_format_as_XL (VALUE_FMT (val));
				gsf_xml_out_add_cstr (state->output, "ValueFormat", fmt);
			}
			g_string_truncate (str, 0);
			value_get_as_gstring (val, str, state->convs);
			gsf_xml_out_add_cstr (state->output, NULL, str->str);
		}

		gsf_xml_out_end_element (state->output); /* </gnm:Item> */
		g_string_free (str, TRUE);
	}

	gsf_xml_out_end_element (state->output); /* </gnm:Scenario> */
}


static void
xml_write_scenarios (GnmOutputXML *state)
{
	GList *ptr;

	if (state->sheet->scenarios == NULL)
		return;

	gsf_xml_out_start_element (state->output, GNM "Scenarios");

	for (ptr = state->sheet->scenarios ; ptr != NULL ; ptr = ptr->next) {
		GnmScenario const *sc = ptr->data;
		xml_write_scenario (state, sc);
	}

	gsf_xml_out_end_element (state->output); /* </gnm:Scenarios> */
}

static int
so_by_pos (SheetObject *a, SheetObject *b)
{
	GnmRange const *ra = &a->anchor.cell_bound;
	GnmRange const *rb = &b->anchor.cell_bound;
	int i;
	i = ra->start.col - rb->start.col;
	if (!i) i = ra->start.row - rb->start.row;
	if (!i) i = ra->end.col - rb->end.col;
	if (!i) i = ra->end.row - rb->end.row;
	return i;
}

static void
xml_write_objects (GnmOutputXML *state, GSList *objects)
{
	gboolean needs_container = TRUE;
	char buffer[4*(DBL_DIG+10)];
	char const *type_name;
	char *tmp;
	GSList *ptr;
	GSList *with_zorder = NULL;
	GSList *without_zorder = NULL;

	/*
	 * Most objects are selectable and the order therefore matters.
	 * We write those in reverse order because sheet_object_set_sheet
	 * will reverse them on input.
	 *
	 * Cell comments are separated out and sorted.  This helps
	 * consistency.
	 *
	 * Yet other objects have no export method and we drop those on
	 * the floor.
	 */
	for (ptr = objects ;ptr != NULL ; ptr = ptr->next) {
		SheetObject *so = ptr->data;
		SheetObjectClass *klass = GNM_SO_CLASS (G_OBJECT_GET_CLASS (so));
		if (klass == NULL || klass->write_xml_sax == NULL)
			continue;

		if (GNM_IS_CELL_COMMENT (so))
			without_zorder = g_slist_prepend (without_zorder, so);
		else
			with_zorder = g_slist_prepend (with_zorder, so);
	}
	without_zorder = g_slist_sort (without_zorder, (GCompareFunc)so_by_pos);
	objects = g_slist_concat (without_zorder, with_zorder);

	for (ptr = objects ;ptr != NULL ; ptr = ptr->next) {
		SheetObject *so = ptr->data;
		SheetObjectClass *klass = GNM_SO_CLASS (G_OBJECT_GET_CLASS (so));
		GnmRange cell_bound = so->anchor.cell_bound;

		switch (so->anchor.mode) {
		case GNM_SO_ANCHOR_TWO_CELLS:
			break;
		case GNM_SO_ANCHOR_ONE_CELL:
			cell_bound.end = cell_bound.start;
			break;
		case GNM_SO_ANCHOR_ABSOLUTE:
			range_init (&cell_bound, 0, 0, 0, 0);
			break;
		default:
			g_assert_not_reached ();
		}

		if (needs_container) {
			needs_container = FALSE;
			gsf_xml_out_start_element (state->output, GNM "Objects");
		}

		/* A hook so that things can sometimes change names */
		type_name = klass->xml_export_name;
		if (type_name == NULL)
			type_name = G_OBJECT_TYPE_NAME (so);

		tmp = g_strconcat (GNM, type_name, NULL);
		gsf_xml_out_start_element (state->output, tmp);
		if (so->name)
			gsf_xml_out_add_cstr (state->output, "Name", so->name);
		if (so->anchor.mode != GNM_SO_ANCHOR_ABSOLUTE)
			gsf_xml_out_add_cstr (state->output, "ObjectBound", range_as_string (&cell_bound));
		if (so->anchor.mode != GNM_SO_ANCHOR_TWO_CELLS)
			gsf_xml_out_add_enum (state->output,
					      "AnchorMode",
					      GNM_SHEET_OBJECT_ANCHOR_MODE_TYPE,
					      so->anchor.mode);
		snprintf (buffer, sizeof (buffer), "%.3g %.3g %.3g %.3g",
			  so->anchor.offset [0], so->anchor.offset [1],
			  so->anchor.offset [2], so->anchor.offset [3]);
		gsf_xml_out_add_cstr (state->output, "ObjectOffset", buffer);

		gsf_xml_out_add_int (state->output, "Direction",
			so->anchor.base.direction);
		gsf_xml_out_add_int
		  (state->output, "Print",
		   sheet_object_get_print_flag (so) ? 1 : 0);

		(*klass->write_xml_sax) (so, state->output, state->convs);

		gsf_xml_out_end_element (state->output); /* </gnm:{typename}> */
		g_free (tmp);
	}
	g_slist_free (objects);

	if (!needs_container)
		gsf_xml_out_end_element (state->output); /* </gnm:Objects> */
}

static void
xml_write_sheet (GnmOutputXML *state, Sheet const *sheet)
{
	GnmColor *c;

	state->sheet = sheet;
	gsf_xml_out_start_element (state->output, GNM "Sheet");

	gsf_xml_out_add_bool (state->output,
		"DisplayFormulas",	sheet->display_formulas);
	gsf_xml_out_add_bool (state->output,
		"HideZero",		sheet->hide_zero);
	gsf_xml_out_add_bool (state->output,
		"HideGrid",		sheet->hide_grid);
	gsf_xml_out_add_bool (state->output,
		"HideColHeader",	sheet->hide_col_header);
	gsf_xml_out_add_bool (state->output,
		"HideRowHeader",	sheet->hide_row_header);
	gsf_xml_out_add_bool (state->output,
		"DisplayOutlines",	sheet->display_outlines);
	gsf_xml_out_add_bool (state->output,
		"OutlineSymbolsBelow",	sheet->outline_symbols_below);
	gsf_xml_out_add_bool (state->output,
		"OutlineSymbolsRight",	sheet->outline_symbols_right);
	if (sheet->text_is_rtl)
		gsf_xml_out_add_bool (state->output,
			"RTL_Layout", sheet->text_is_rtl);
	if (sheet->is_protected)
		gsf_xml_out_add_bool (state->output,
			"Protected", sheet->is_protected);

	/* TODO : Make this an enum internally eventually */
	if (sheet->convs->r1c1_addresses)
		gsf_xml_out_add_cstr_unchecked (state->output,
			"ExprConvention", "gnumeric:R1C1");

	gsf_xml_out_add_enum (state->output,
		"Visibility", GNM_SHEET_VISIBILITY_TYPE, sheet->visibility);

	if (sheet->tab_color != NULL)
		gnm_xml_out_add_color (state->output, "TabColor", sheet->tab_color);
	if (sheet->tab_text_color != NULL)
		gnm_xml_out_add_color (state->output, "TabTextColor", sheet->tab_text_color);
	if (NULL != (c = sheet_style_get_auto_pattern_color (sheet))) {
		gnm_xml_out_add_color (state->output, "GridColor", c);
		style_color_unref (c);
	}

	gsf_xml_out_simple_element (state->output,
		GNM "Name", sheet->name_unquoted);
	gsf_xml_out_simple_int_element (state->output,
		GNM "MaxCol", sheet->cols.max_used);
	gsf_xml_out_simple_int_element (state->output,
		GNM "MaxRow", sheet->rows.max_used);
	gsf_xml_out_simple_float_element (state->output,
		GNM "Zoom", sheet->last_zoom_factor_used, 4);

	xml_write_named_expressions (state, sheet->names);
	xml_write_print_info (state, sheet->print_info);
	xml_write_styles (state);
	xml_write_cols_rows (state, NULL);
	xml_write_selection_info (state);
	xml_write_objects (state, sheet->sheet_objects);
	xml_write_cells (state);

	xml_write_merged_regions (state);
	xml_write_sheet_layout (state);
	xml_write_sheet_filters (state);
	xml_write_solver (state);
	xml_write_scenarios (state);

	gnm_xml_out_end_element_check (state->output, GNM "Sheet");
	state->sheet = NULL;
}

static void
xml_write_sheets (GnmOutputXML *state)
{
	int i, n = workbook_sheet_count (state->wb);
	gsf_xml_out_start_element (state->output, GNM "Sheets");
	for (i = 0 ; i < n ; i++)
		xml_write_sheet (state, workbook_sheet_by_index (state->wb, i));
	gsf_xml_out_end_element (state->output); /* </gnm:Sheets> */
}

static void
xml_write_uidata (GnmOutputXML *state)
{
	gsf_xml_out_start_element (state->output, GNM "UIData");
	gsf_xml_out_add_int (state->output, "SelectedTab",
		wb_view_cur_sheet (state->wb_view)->index_in_wb);
	gsf_xml_out_end_element (state->output); /* </gnm:UIData> */
}

static void
xml_write_date_conventions_as_attr (GnmOutputXML *state,
				    GODateConventions const *conv)
{
	if (conv->use_1904)
		gsf_xml_out_add_cstr_unchecked (state->output,
			GNM "DateConvention", "Apple:1904");
}

static void
xml_write_number_system (GnmOutputXML *state)
{
	/*
	 * These numbers define how to interpret decimal values in the
	 * file.  They are not yet used, but should be used when the
	 * number system of the loading Gnumeric is different from the
	 * number system of the saving Gnumeric.
	 */
	gsf_xml_out_add_int (state->output, "FloatRadix", GNM_RADIX);
	gsf_xml_out_add_int (state->output, "FloatDigits", GNM_MANT_DIG);
}


static void
xml_write_calculation (GnmOutputXML *state)
{
	gsf_xml_out_start_element (state->output, GNM "Calculation");
	gsf_xml_out_add_bool (state->output,
		"ManualRecalc",		!state->wb->recalc_auto);
	gsf_xml_out_add_bool (state->output,
		"EnableIteration",	state->wb->iteration.enabled);
	gsf_xml_out_add_int (state->output,
		"MaxIterations",	state->wb->iteration.max_number);
	gnm_xml_out_add_gnm_float (state->output,
		"IterationTolerance",	state->wb->iteration.tolerance);
	xml_write_date_conventions_as_attr (state,
					    workbook_date_conv (state->wb));
	xml_write_number_system (state);
	gsf_xml_out_end_element (state->output); /* </gnm:Calculation> */
}

GnmConventions *
gnm_xml_io_conventions (void)
{
	GnmConventions *res = gnm_conventions_new ();

	res->decimal_sep_dot	= TRUE;
	res->input.range_ref	= rangeref_parse;
	res->output.range_ref	= gnm_1_0_rangeref_as_string;
	res->range_sep_colon	= TRUE;
	res->arg_sep		= ',';
	res->array_col_sep	= ',';
	res->array_row_sep	= ';';
	res->output.translated	= FALSE;
	res->output.uppercase_E = FALSE;

	if (!gnm_shortest_rep_in_files ()) {
		gnm_float l10 = gnm_log10 (GNM_RADIX);
		res->output.decimal_digits =
			(int)gnm_ceil (GNM_MANT_DIG * l10) +
			(l10 == (int)l10 ? 0 : 1);
	}

	return res;
}

static void
gnm_xml_file_save_full (G_GNUC_UNUSED GOFileSaver const *fs,
			G_GNUC_UNUSED GOIOContext *io_context,
			GoView const *view, GsfOutput *output,
			gboolean compress)
{
	GnmOutputXML state;
	GsfOutput   *gzout = NULL;
	GnmLocale   *locale;
	WorkbookView *wb_view = GNM_WORKBOOK_VIEW (view);

	if (compress) {
		gzout  = gsf_output_gzip_new (output, NULL);
		output = gzout;
	}

	state.wb_view	= wb_view;
	state.wb	= wb_view_get_workbook (wb_view);
	state.sheet	= NULL;
	state.output	= gsf_xml_out_new (output);
	state.convs	= gnm_xml_io_conventions ();
	state.expr_map  = g_hash_table_new (g_direct_hash, g_direct_equal);
	state.cell_str  = g_string_new (NULL);
	state.write_value_result = FALSE;
	go_doc_init_write (GO_DOC (state.wb), state.output);

	locale = gnm_push_C_locale ();

	gsf_xml_out_start_element (state.output, GNM "Workbook");

	/*
	 * As long as we want older versions of Gnumeric to be able to read
	 * the files we produce, we should not increase the version number
	 * in the file we write.  Until 1.12.21, v10 was the highest listed
	 * xml-sax-read.c's content_ns.
	 */
	gsf_xml_out_add_cstr_unchecked (state.output, "xmlns:gnm",
		"http://www.gnumeric.org/v10.dtd");
#if 0 /* seems to break meta data */
	/* default namespace added for 1.8 */
	gsf_xml_out_add_cstr_unchecked (state.output, "xmlns",
		"http://www.gnumeric.org/v10.dtd");
#endif
	gsf_xml_out_add_cstr_unchecked (state.output, "xmlns:xsi",
		"http://www.w3.org/2001/XMLSchema-instance");
	gsf_xml_out_add_cstr_unchecked (state.output, "xsi:schemaLocation",
		"http://www.gnumeric.org/v9.xsd");

	xml_write_version	    (&state);
	xml_write_attributes	    (&state);
	xml_write_meta_data	    (&state);
	xml_write_conventions	    (&state);	/* DEPRECATED, moved to Calculation */
	xml_write_calculation	    (&state);
	xml_write_sheet_names	    (&state);
	xml_write_named_expressions (&state, state.wb->names);
	xml_write_geometry	    (&state);
	xml_write_sheets	    (&state);
	xml_write_uidata	    (&state);
	go_doc_write (GO_DOC (state.wb), state.output);

	gsf_xml_out_end_element (state.output); /* </Workbook> */

	gnm_pop_C_locale (locale);

	g_hash_table_destroy (state.expr_map);
	g_string_free (state.cell_str, TRUE);
	gnm_conventions_unref (state.convs);
	g_object_unref (state.output);

	if (gzout) {
		gsf_output_close (gzout);
		g_object_unref (gzout);
	}
}

static void
gnm_xml_file_save (GOFileSaver const *fs, GOIOContext *io_context,
		   GoView const *view, GsfOutput *output)
{
	gboolean compress;
	char const  *extension = NULL;

	/* If the suffix is .xml disable compression */
	if (NULL != gsf_output_name (output))
		extension = gsf_extension_pointer (gsf_output_name (output));
	if (NULL != extension && g_ascii_strcasecmp (extension, "xml") == 0)
		compress = FALSE;
	else
		compress = (gnm_conf_get_core_xml_compression_level () > 0);

	gnm_xml_file_save_full (fs, io_context, view, output, compress);
}

static void
gnm_xml_file_save_xml (GOFileSaver const *fs, GOIOContext *io_context,
		   GoView const *view, GsfOutput *output)
{
	gnm_xml_file_save_full (fs, io_context, view, output, FALSE);
}

/**************************************************************************/

typedef struct {
	GnmOutputXML  state;
	GnmCellRegion const *cr;
	GnmParsePos   pp;
} XMLCellCopyState;

static void
cb_xml_write_cell_region_cells (GnmCellCopy *cc,
				G_GNUC_UNUSED gconstpointer ignore,
				XMLCellCopyState *state)
{
	state->pp.eval.col = state->cr->base.col + cc->offset.col;
	state->pp.eval.row = state->cr->base.row + cc->offset.row;
	xml_write_cell_and_position (&state->state,
		cc->texpr, cc->val, &state->pp);
}

static int
by_row_col (GnmCellCopy *cc_a, gpointer val_a,
	    GnmCellCopy *cc_b, gpointer val_b,
	    gpointer user)
{
	int res = cc_a->offset.row - cc_b->offset.row;
	if (!res)
		res = cc_a->offset.col - cc_b->offset.col;
	return res;
}

/**
 * gnm_cellregion_to_xml:
 * @cr: the content to store.
 *
 * Returns: (transfer full): %NULL on error
 **/
GsfOutputMemory *
gnm_cellregion_to_xml (GnmCellRegion const *cr)
{
	XMLCellCopyState state;
	GnmStyleList *s_ptr;
	GSList       *ptr;
	GsfOutput    *buf = gsf_output_memory_new ();
	GnmLocale    *locale;
	GODoc	     *doc = NULL;

	g_return_val_if_fail (cr != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (cr->origin_sheet), NULL);

	state.state.wb_view = NULL;
	state.state.wb = NULL;
	state.state.sheet = cr->origin_sheet;
	state.state.output = gsf_xml_out_new (buf);
	state.state.convs = gnm_xml_io_conventions ();
	state.state.expr_map = g_hash_table_new (g_direct_hash, g_direct_equal);
	state.state.cell_str = g_string_new (NULL);
	state.state.write_value_result = TRUE;

	locale = gnm_push_C_locale ();
	if (cr->origin_sheet) {
		/* hoping this always occur */
		doc = GO_DOC (cr->origin_sheet->workbook);
		go_doc_init_write (doc, state.state.output);
	}

	gsf_xml_out_start_element (state.state.output, GNM "ClipboardRange");

	/* backwards compat, must be first */
	gsf_xml_out_add_cstr_unchecked (state.state.output, "xmlns:gnm",
		"http://www.gnumeric.org/v10.dtd");
	/* default namespace added for 1.8 */
	gsf_xml_out_add_cstr_unchecked (state.state.output, "xmlns",
		"http://www.gnumeric.org/v10.dtd");

	gsf_xml_out_add_int (state.state.output, "Cols", cr->cols);
	gsf_xml_out_add_int (state.state.output, "Rows", cr->rows);
	gsf_xml_out_add_int (state.state.output, "BaseCol", cr->base.col);
	gsf_xml_out_add_int (state.state.output, "BaseRow", cr->base.row);
	if (cr->origin_sheet)
		xml_write_date_conventions_as_attr
			(&state.state,
			 sheet_date_conv (cr->origin_sheet));
	xml_write_number_system (&state.state);
	if (cr->not_as_contents)
		gsf_xml_out_add_bool (state.state.output, "NotAsContent", TRUE);

	xml_write_cols_rows (&state.state, cr);

	if (cr->styles != NULL) {
		gsf_xml_out_start_element (state.state.output, GNM "Styles");
		for (s_ptr = cr->styles ; s_ptr != NULL ; s_ptr = s_ptr->next)
			xml_write_style_region (&state.state, s_ptr->data);
		gsf_xml_out_end_element (state.state.output); /* </Styles> */
	}

	if (cr->merged != NULL) {
		gsf_xml_out_start_element (state.state.output, GNM "MergedRegions");
		for (ptr = cr->merged ; ptr != NULL ; ptr = ptr->next) {
			gsf_xml_out_start_element (state.state.output, GNM "Merge");
			gsf_xml_out_add_cstr_unchecked (state.state.output, NULL,
				range_as_string (ptr->data));
			gsf_xml_out_end_element (state.state.output); /* </Merge> */
		}
		gsf_xml_out_end_element (state.state.output); /* </gnm:MergedRegions> */
	}

	/* NOTE SNEAKY : ensure that sheet names have explicit workbooks */
	state.pp.wb    = NULL;
	state.pp.sheet = cr->origin_sheet;
	state.cr = cr;
	if (cr->cell_content != NULL) {
		gsf_xml_out_start_element (state.state.output, GNM "Cells");
		gnm_hash_table_foreach_ordered
			(cr->cell_content,
			 (GHFunc) cb_xml_write_cell_region_cells,
			 (GnmHashTableOrder)by_row_col,
			 &state);
		gsf_xml_out_end_element (state.state.output); /* </Cells> */
	}

	xml_write_objects (&state.state, cr->objects);

	if (NULL != doc)
		go_doc_write (doc, state.state.output);
	gsf_xml_out_end_element (state.state.output); /* </ClipboardRange> */

	gnm_pop_C_locale (locale);

	g_hash_table_destroy (state.state.expr_map);
	g_string_free (state.state.cell_str, TRUE);
	gnm_conventions_unref (state.state.convs);
	g_object_unref (state.state.output);

	gsf_output_close (buf);

	return GSF_OUTPUT_MEMORY (buf);
}

#define XML_SAX_ID "Gnumeric_XmlIO:sax"
#define XML_SAX_ID_0 "Gnumeric_XmlIO:sax:0"

void
gnm_xml_sax_write_init (void)
{
	GOFileSaver *saver = go_file_saver_new
		(XML_SAX_ID,
		 "gnumeric",
		 _("Gnumeric XML (*.gnumeric)"),
		 GO_FILE_FL_AUTO, gnm_xml_file_save);
	g_object_set (G_OBJECT (saver),
		      "mime-type", "application/x-gnumeric",
		      NULL);

	go_file_saver_register_as_default (saver, 50);
	g_object_unref (saver);

	saver = go_file_saver_new
		(XML_SAX_ID_0,
		 "xml",
		 _("Gnumeric XML uncompressed (*.xml)"),
		 GO_FILE_FL_AUTO, gnm_xml_file_save_xml);
	g_object_set (G_OBJECT (saver),
		      "mime-type", "application/xml",
		      NULL);

	go_file_saver_register (saver);
	g_object_unref (saver);
}

void
gnm_xml_sax_write_shutdown (void)
{
	go_file_saver_unregister (go_file_saver_for_id (XML_SAX_ID));
	go_file_saver_unregister (go_file_saver_for_id (XML_SAX_ID_0));
}

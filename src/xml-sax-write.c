/* vim: set sw=8: */

/*
 * xml-sax-write.c : export .gnumeric and the clipboard subset using a the sax
 *			like wrappers in libgsf
 *
 * Copyright (C) 2003-2007 Jody Goldberg (jody@gnome.org)
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
#include <gnumeric.h>
#include <xml-sax.h>
#include <workbook-view.h>
#include <gnm-format.h>
#include <workbook.h>
#include <workbook-priv.h> /* Workbook::names */
#include <cell.h>
#include <sheet.h>
#include <sheet-view.h>
#include <sheet-style.h>
#include <style-color.h>
#include <style-conditions.h>
#include <expr.h>
#include <expr-impl.h>
#include <expr-name.h>
#include <value.h>
#include <str.h>
#include <ranges.h>
#include <mstyle.h>
#include <style-border.h>
#include <validation.h>
#include <hlink.h>
#include <input-msg.h>
#include <solver.h>
#include <sheet-filter.h>
#include <sheet-object-impl.h>
#include <print-info.h>
#include <xml-io.h>
#include <gutils.h>
#include <clipboard.h>
#include <tools/scenarios.h>
#include <gnumeric-gconf.h>

#include <goffice/app/file.h>
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

	GsfXMLOut *output;
} GnmOutputXML;

#define GNM "gnm:"

/* Precision to use when saving point measures. */
#define POINT_SIZE_PRECISION 4

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
xml_write_attribute (GnmOutputXML *state, char const *name, char const *value)
{
	gsf_xml_out_start_element (state->output, GNM "Attribute");
	/* backwards compatibility with 1.0.x which uses gtk-1.2 GTK_TYPE_BOOLEAN */
	gsf_xml_out_simple_element (state->output, GNM "type", "4");
	gsf_xml_out_simple_element (state->output, GNM "name", name);
	gsf_xml_out_simple_element (state->output, GNM "value", value);
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
	xml_write_attribute (state, "WorkbookView::show_horizontal_scrollbar",
		state->wb_view->show_horizontal_scrollbar ? "TRUE" : "FALSE");
	xml_write_attribute (state, "WorkbookView::show_vertical_scrollbar",
		state->wb_view->show_vertical_scrollbar ? "TRUE" : "FALSE");
	xml_write_attribute (state, "WorkbookView::show_notebook_tabs",
		state->wb_view->show_notebook_tabs ? "TRUE" : "FALSE");
	xml_write_attribute (state, "WorkbookView::do_auto_completion",
		state->wb_view->do_auto_completion ? "TRUE" : "FALSE");
	xml_write_attribute (state, "WorkbookView::is_protected",
		state->wb_view->is_protected ? "TRUE" : "FALSE");
	gsf_xml_out_end_element (state->output); /* </Attributes> */
}

static void
xml_write_meta_data (GnmOutputXML *state)
{
	gsf_opendoc_metadata_write (state->output,
		go_doc_get_meta_data (GO_DOC (state->wb)));
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
		nexpr->name->str);
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
	if (scope != NULL) {
		GSList *names =
			g_slist_sort (gnm_named_expr_collection_list (scope),
				      (GCompareFunc)expr_name_cmp_by_name);
		GSList *p;

		gsf_xml_out_start_element (state->output, GNM "Names");
		for (p = names; p; p = p->next) {
			GnmNamedExpr *nexpr = p->data;
			xml_write_name (state, nexpr);
		}
		gsf_xml_out_end_element (state->output); /* </gnm:Names> */
		g_slist_free (names);
	}
}

static void
xml_write_geometry (GnmOutputXML *state)
{
	gsf_xml_out_start_element (state->output, GNM "Geometry");
	gsf_xml_out_add_int (state->output, "Width", state->wb_view->preferred_width);
	gsf_xml_out_add_int (state->output, "Height", state->wb_view->preferred_height);
	gsf_xml_out_end_element (state->output); /* </gnm:Geometry> */
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
			      PrintRepeatRange *range)
{
	if (range->use) {
		gsf_xml_out_start_element (state->output, name);
		gsf_xml_out_add_cstr_unchecked (state->output, "value",
			range_as_string (&range->range));
		gsf_xml_out_end_element (state->output);
	}
}

static void
xml_write_print_hf (GnmOutputXML *state, char const *name,
		    PrintHF const *hf)
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
		gsf_xml_out_end_element (state->output); /* </break> */
	}
	gsf_xml_out_end_element (state->output);
}

static void
xml_write_print_info (GnmOutputXML *state, PrintInformation *pi)
{
	char  *paper_name;
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
		gsf_xml_out_add_float  (state->output, "percentage", pi->scaling.percentage.x, -1);
	} else {
		gsf_xml_out_add_cstr_unchecked  (state->output, "type", "size_fit");
		gsf_xml_out_add_float  (state->output, "cols", pi->scaling.dim.cols, -1);
		gsf_xml_out_add_float  (state->output, "rows", pi->scaling.dim.rows, -1);
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


	xml_write_print_repeat_range (state, GNM "repeat_top", &pi->repeat_top);
	xml_write_print_repeat_range (state, GNM "repeat_left", &pi->repeat_left);

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
		gsf_xml_out_simple_element (state->output, GNM "paper", paper_name);
	g_free (paper_name);

	if (NULL != pi->page_breaks.v)
		xml_write_breaks (state, pi->page_breaks.v);
	if (NULL != pi->page_breaks.h)
		xml_write_breaks (state, pi->page_breaks.h);
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
		GNM "Diagonal",
		GNM "Rev-Diagonal"
	};
	GnmValidation const *v;
	GnmHLink   const *link;
	GnmInputMsg const *im;
	GnmStyleConditions const *sc;
	GnmStyleCond const *cond;
	GnmBorder const *border;
	GnmStyleBorderType t;
	GnmParsePos    pp;
	char	   *tmp;
	unsigned i;
	gboolean started;

	gsf_xml_out_start_element (state->output, GNM "Style");

	if (gnm_style_is_element_set (style, MSTYLE_ALIGN_H))
		gsf_xml_out_add_int (state->output, "HAlign", gnm_style_get_align_h (style));
	if (gnm_style_is_element_set (style, MSTYLE_ALIGN_V))
		gsf_xml_out_add_int (state->output, "VAlign", gnm_style_get_align_v (style));
	if (gnm_style_is_element_set (style, MSTYLE_WRAP_TEXT))
		gsf_xml_out_add_bool (state->output, "WrapText", gnm_style_get_wrap_text (style));
	if (gnm_style_is_element_set (style, MSTYLE_SHRINK_TO_FIT))
		gsf_xml_out_add_bool (state->output, "ShrinkToFit", gnm_style_get_shrink_to_fit (style));
	if (gnm_style_is_element_set (style, MSTYLE_ROTATION))
		gsf_xml_out_add_int (state->output, "Rotation", gnm_style_get_rotation (style));
	if (gnm_style_is_element_set (style, MSTYLE_PATTERN))
		gsf_xml_out_add_int (state->output, "Shade", gnm_style_get_pattern (style));
	if (gnm_style_is_element_set (style, MSTYLE_INDENT))
		gsf_xml_out_add_int (state->output, "Indent", gnm_style_get_indent (style));
	if (gnm_style_is_element_set (style, MSTYLE_CONTENTS_LOCKED))
		gsf_xml_out_add_bool (state->output, "Locked", gnm_style_get_contents_locked (style));
	if (gnm_style_is_element_set (style, MSTYLE_CONTENTS_HIDDEN))
		gsf_xml_out_add_bool (state->output, "Hidden", gnm_style_get_contents_hidden (style));
	if (gnm_style_is_element_set (style, MSTYLE_FONT_COLOR))
		gnm_xml_out_add_color (state->output, "Fore", gnm_style_get_font_color (style));
	if (gnm_style_is_element_set (style, MSTYLE_COLOR_BACK))
		gnm_xml_out_add_color (state->output, "Back", gnm_style_get_back_color (style));
	if (gnm_style_is_element_set (style, MSTYLE_COLOR_PATTERN))
		gnm_xml_out_add_color (state->output, "PatternColor", gnm_style_get_pattern_color (style));
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
		char const *fontname;

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

		if (gnm_style_is_element_set (style, MSTYLE_FONT_NAME))
			fontname = gnm_style_get_font_name (style);
		else /* backwards compatibility */
			fontname = "Helvetica";

		gsf_xml_out_add_cstr (state->output, NULL, fontname);
		gsf_xml_out_end_element (state->output);
	}

	if (gnm_style_is_element_set (style, MSTYLE_HLINK) &&
	    NULL != (link = gnm_style_get_hlink (style))) {
		gsf_xml_out_start_element (state->output, GNM "HyperLink");
		gsf_xml_out_add_cstr (state->output, "type", g_type_name (G_OBJECT_TYPE (link)));
		gsf_xml_out_add_cstr (state->output, "target", gnm_hlink_get_target (link));
		if (gnm_hlink_get_tip (link) != NULL)
			gsf_xml_out_add_cstr (state->output, "tip", gnm_hlink_get_tip (link));
		gsf_xml_out_end_element (state->output);
	}

	if (gnm_style_is_element_set (style, MSTYLE_VALIDATION) &&
	    NULL != (v = gnm_style_get_validation (style))) {
		gsf_xml_out_start_element (state->output, GNM "Validation");
		gsf_xml_out_add_int (state->output, "Style", v->style);
		gsf_xml_out_add_int (state->output, "Type", v->type);

		switch (v->type) {
		case VALIDATION_TYPE_AS_INT :
		case VALIDATION_TYPE_AS_NUMBER :
		case VALIDATION_TYPE_AS_DATE :
		case VALIDATION_TYPE_AS_TIME :
		case VALIDATION_TYPE_TEXT_LENGTH :
			gsf_xml_out_add_int (state->output, "Operator", v->op);
			break;
		default :
			break;
		}

		gsf_xml_out_add_bool (state->output, "AllowBlank", v->allow_blank);
		gsf_xml_out_add_bool (state->output, "UseDropdown", v->use_dropdown);

		if (v->title != NULL && v->title->str[0] != '\0')
			gsf_xml_out_add_cstr (state->output, "Title", v->title->str);
		if (v->msg != NULL && v->msg->str[0] != '\0')
			gsf_xml_out_add_cstr (state->output, "Message", v->msg->str);

		parse_pos_init_sheet (&pp, (Sheet *)state->sheet);
		if (v->texpr[0] != NULL &&
		    (tmp = gnm_expr_top_as_string (v->texpr[0], &pp, state->convs)) != NULL) {
			gsf_xml_out_simple_element (state->output, GNM "Expression0", tmp);
			g_free (tmp);
		}
		if (v->texpr[1] != NULL &&
		    (tmp = gnm_expr_top_as_string (v->texpr[1], &pp, state->convs)) != NULL) {
			gsf_xml_out_simple_element (state->output, GNM "Expression1", tmp);
			g_free (tmp);
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
		GArray const *conds = gnm_style_conditions_details (sc);
		if (conds != NULL)
			for (i = 0 ; i < conds->len ; i++) {
				cond = &g_array_index (conds, GnmStyleCond, i);
				gsf_xml_out_start_element (state->output, GNM "Condition");
				gsf_xml_out_add_int (state->output, "Operator", cond->op);
				parse_pos_init_sheet (&pp, (Sheet *)state->sheet);
				if (cond->texpr[0] != NULL &&
				    (tmp = gnm_expr_top_as_string (cond->texpr[0], &pp, state->convs)) != NULL) {
					gsf_xml_out_simple_element (state->output, GNM "Expression0", tmp);
					g_free (tmp);
				}
				if (cond->texpr[1] != NULL &&
				    (tmp = gnm_expr_top_as_string (cond->texpr[1], &pp, state->convs)) != NULL) {
					gsf_xml_out_simple_element (state->output, GNM "Expression1", tmp);
					g_free (tmp);
				}
				xml_write_style (state, cond->overlay);
				gsf_xml_out_end_element (state->output); /* </Condition> */
			}
	}

	started = FALSE;
	for (i = MSTYLE_BORDER_TOP; i <= MSTYLE_BORDER_DIAGONAL; i++)
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
		g_slist_sort (sheet_style_get_list (state->sheet, NULL),
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
	gboolean	is_column;
	ColRowInfo const *prev;
	int prev_pos, rle_count;
} closure_write_colrow;

static gboolean
xml_write_colrow_info (GnmColRowIter const *iter, closure_write_colrow *closure)
{
	ColRowInfo const *prev = closure->prev;
	GsfXMLOut *output = closure->state->output;

	closure->rle_count++;
	if (NULL != iter && colrow_equal (prev, iter->cri))
		return FALSE;

	if (prev != NULL) {
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
		closure->prev = iter->cri;
		closure->prev_pos = iter->pos;
	}

	return FALSE;
}

static void
xml_write_cols_rows (GnmOutputXML *state)
{
	closure_write_colrow closure;
	gsf_xml_out_start_element (state->output, GNM "Cols");
	xml_out_add_points (state->output, "DefaultSizePts",
		sheet_col_get_default_size_pts (state->sheet));
	closure.state = state;
	closure.is_column = TRUE;
	closure.prev = NULL;
	closure.prev_pos = -1;
	closure.rle_count = 0;
	colrow_foreach (&state->sheet->cols, 0, gnm_sheet_get_max_cols (state->sheet)-1,
		(ColRowHandler)&xml_write_colrow_info, &closure);
	xml_write_colrow_info (NULL, &closure); /* flush */
	gsf_xml_out_end_element (state->output); /* </gnm:Cols> */

	gsf_xml_out_start_element (state->output, GNM "Rows");
	xml_out_add_points (state->output, "DefaultSizePts",
		sheet_row_get_default_size_pts (state->sheet));
	closure.state = state;
	closure.is_column = FALSE;
	closure.prev = NULL;
	closure.prev_pos = -1;
	closure.rle_count = 0;
	colrow_foreach (&state->sheet->rows, 0, gnm_sheet_get_max_rows (state->sheet)-1,
		(ColRowHandler)&xml_write_colrow_info, &closure);
	xml_write_colrow_info (NULL, &closure); /* flush */
	gsf_xml_out_end_element (state->output); /* </gnm:Rows> */
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
	copy = g_slist_copy (sv->selections);
	ptr = g_slist_reverse (copy);
	for (; ptr != NULL ; ptr = ptr->next) {
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
		gsf_xml_out_add_int (state->output, "Rows", texpr->expr->array_corner.rows);
		gsf_xml_out_add_int (state->output, "Cols", texpr->expr->array_corner.cols);
	}

	if (write_contents) {
		GString *str = state->cell_str;

		g_string_truncate (str, 0);

		if (!texpr) {
			if (val != NULL) {
				gsf_xml_out_add_int (state->output, "ValueType", val->type);
				if (VALUE_FMT (val) != NULL) {
					const char *fmt = go_format_as_XL (VALUE_FMT (val));
					gsf_xml_out_add_cstr (state->output, "ValueFormat", fmt);
				}
				value_get_as_gstring (val, str, state->convs);
			} else {
				g_warning ("%s has no value ?", cellpos_as_string (&pp->eval));
			}
		} else {
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
	xml_write_cell_and_position (state,
		iter->cell->base.texpr, iter->cell->value, &iter->pp);
	return NULL;
}

static void
xml_write_cells (GnmOutputXML *state)
{
	gsf_xml_out_start_element (state->output, GNM "Cells");
	sheet_foreach_cell_in_range ((Sheet *)state->sheet, CELL_ITER_IGNORE_NONEXISTENT,
		0, 0, gnm_sheet_get_max_cols (state->sheet)-1, gnm_sheet_get_max_rows (state->sheet)-1,
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

	if (sv_is_frozen (sv)) {
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
		filter_expr_attrs[i].valtype, cond->value[i]->type);
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
			cond->op[0] & 1 ? TRUE : FALSE);
		gsf_xml_out_add_bool (state->output, "items",
			cond->op[0] & 2 ? TRUE : FALSE);
		gsf_xml_out_add_float (state->output, "count", cond->count, 4);
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
        SolverParameters *param = state->sheet->solver_parameters;
	SolverConstraint const *c;
	int type;
	GSList *ptr;

	if (param == NULL)
		return;

	gsf_xml_out_start_element (state->output, GNM "Solver");

	if (param->target_cell != NULL) {
		gsf_xml_out_add_int (state->output, "TargetCol",
			param->target_cell->pos.col);
		gsf_xml_out_add_int (state->output, "TargetRow",
			param->target_cell->pos.row);
	}

	gsf_xml_out_add_int (state->output, "ProblemType", param->problem_type);
	gsf_xml_out_add_cstr (state->output, "Inputs",
		param->input_entry_str);
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
	gsf_xml_out_add_bool (state->output, "ShowIter",
		param->options.show_iter_results);
	gsf_xml_out_add_bool (state->output, "AnswerR",
		param->options.answer_report);
	gsf_xml_out_add_bool (state->output, "SensitivityR",
		param->options.sensitivity_report);
	gsf_xml_out_add_bool (state->output, "LimitsR",
		param->options.limits_report);
	gsf_xml_out_add_bool (state->output, "PerformR",
		param->options.performance_report);
	gsf_xml_out_add_bool (state->output, "ProgramR",
		param->options.program_report);

	for (ptr = param->constraints; ptr != NULL ; ptr = ptr->next) {
		c = ptr->data;

		gsf_xml_out_start_element (state->output, GNM "Constr");
		gsf_xml_out_add_int (state->output, "Lcol", c->lhs.col);
		gsf_xml_out_add_int (state->output, "Lrow", c->lhs.row);
		gsf_xml_out_add_int (state->output, "Rcol", c->rhs.col);
		gsf_xml_out_add_int (state->output, "Rrow", c->rhs.row);
		gsf_xml_out_add_int (state->output, "Cols", c->cols);
		gsf_xml_out_add_int (state->output, "Rows", c->rows);

		switch (c->type) {
		default:	 type = 0;	break;
		case SolverLE:   type = 1;	break;
		case SolverGE:   type = 2;	break;
		case SolverEQ:   type = 4;	break;
		case SolverINT:  type = 8;	break;
		case SolverBOOL: type = 16;	break;
		}
		gsf_xml_out_add_int (state->output, "Type", type);
		gsf_xml_out_end_element (state->output); /* </gnm:Constr> */
	}

	gsf_xml_out_end_element (state->output); /* </gnm:Solver> */
}

static void
xml_write_scenarios (GnmOutputXML *state)
{
	GList   *ptr;

	if (state->sheet->scenarios == NULL)
		return;

	gsf_xml_out_start_element (state->output, GNM "Scenarios");

	for (ptr = state->sheet->scenarios ; ptr != NULL ; ptr = ptr->next) {
		scenario_t const *s = (scenario_t const *)ptr->data;
#if 0
		int       i, cols, rows;
#endif

		gsf_xml_out_start_element (state->output, GNM "Scenario");
		gsf_xml_out_add_cstr (state->output, "Name", s->name);
		gsf_xml_out_add_cstr (state->output, "Comment", s->comment);

		/* Scenario: changing cells in a string form.  In a string
		 * form so that we can in the future allow it to contain
		 * multiple ranges without modifing the file format.*/
		gsf_xml_out_add_cstr (state->output, "CellsStr", s->cell_sel_str);

#if 0 /* CRACK CRACK CRACK need something cleaner */
		/* Scenario: values. */
		rows = range_height (&s->range);
		cols = range_width (&s->range);
		for (i = 0; i < cols * rows; i++) {
			GString  *name = g_string_new (NULL);
			g_string_append_printf (name, "V%d", i);
			xml_node_set_value (scen, name->str,
					    s->changing_cells [i]);
			g_string_free (name, TRUE);
		}
#endif

		gsf_xml_out_end_element (state->output); /* </gnm:Scenario> */
	}

	gsf_xml_out_end_element (state->output); /* </gnm:Scenarios> */
}

static void
xml_write_objects (GnmOutputXML *state, GSList *objects)
{
	gboolean needs_container = TRUE;
	SheetObject	 *so;
	SheetObjectClass *klass;
	char buffer[4*(DBL_DIG+10)];
	char const *type_name;
	char *tmp;
	GSList *ptr;

	/* reverse the list to maintain order when we prepend the objects in
	 * sheet_object_set_sheet on import */
	objects = g_slist_reverse ( g_slist_copy (objects));
	for (ptr = objects ;ptr != NULL ; ptr = ptr->next) {
		so = ptr->data;
		klass = SHEET_OBJECT_CLASS (G_OBJECT_GET_CLASS (so));
		if (klass == NULL || klass->write_xml_sax == NULL)
			continue;

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
		gsf_xml_out_add_cstr (state->output, "ObjectBound", range_as_string (&so->anchor.cell_bound));
		snprintf (buffer, sizeof (buffer), "%.3g %.3g %.3g %.3g",
			  so->anchor.offset [0], so->anchor.offset [1],
			  so->anchor.offset [2], so->anchor.offset [3]);
		gsf_xml_out_add_cstr (state->output, "ObjectOffset", buffer);

		gsf_xml_out_add_int (state->output, "Direction",
			so->anchor.base.direction);

		(*klass->write_xml_sax) (so, state->output);

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
	xml_write_cols_rows (state);
	xml_write_selection_info (state);
	xml_write_objects (state, sheet->sheet_objects);
	xml_write_cells (state);

	xml_write_merged_regions (state);
	xml_write_sheet_layout (state);
	xml_write_sheet_filters (state);
	xml_write_solver (state);
	xml_write_scenarios (state);

	gsf_xml_out_end_element (state->output); /* </gnm:Sheet> */
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
xml_write_calculation (GnmOutputXML *state)
{
	gsf_xml_out_start_element (state->output, GNM "Calculation");
	gsf_xml_out_add_bool (state->output,
		"ManualRecalc",		!state->wb->recalc_auto);
	gsf_xml_out_add_bool (state->output,
		"EnableIteration",	state->wb->iteration.enabled);
	gsf_xml_out_add_int (state->output,
		"MaxIterations",	state->wb->iteration.max_number);
	gsf_xml_out_add_float (state->output,
		"IterationTolerance",	state->wb->iteration.tolerance, -1);
	xml_write_date_conventions_as_attr (state,
					    workbook_date_conv (state->wb));
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

	return res;
}

void
gnm_xml_file_save (GOFileSaver const *fs, IOContext *io_context,
		   gconstpointer wb_view, GsfOutput *output)
{
	GnmOutputXML state;
	char const  *extension = NULL;
	GsfOutput   *gzout = NULL;
	GnmLocale   *locale;
	gboolean     compress;

	/* If the suffix is .xml disable compression */
	if (NULL != gsf_output_name (output))
		extension = gsf_extension_pointer (gsf_output_name (output));
	if (NULL != extension && g_ascii_strcasecmp (extension, "xml") == 0)
		compress = FALSE;
	else
		compress = (gnm_app_prefs->xml_compression_level > 0);

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

	locale = gnm_push_C_locale ();

	gsf_xml_out_start_element (state.output, GNM "Workbook");

	/* backwards compat, must be first */
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
	xml_write_sheet_names	    (&state);
	xml_write_named_expressions (&state, state.wb->names);
	xml_write_geometry	    (&state);
	xml_write_sheets	    (&state);
	xml_write_uidata	    (&state);
	xml_write_calculation	    (&state);

	gsf_xml_out_end_element (state.output); /* </Workbook> */

	gnm_pop_C_locale (locale);

	g_hash_table_destroy (state.expr_map);
	g_string_free (state.cell_str, TRUE);
	gnm_conventions_free (state.convs);
	g_object_unref (G_OBJECT (state.output));

	if (gzout) {
		gsf_output_close (gzout);
		g_object_unref (gzout);
	}
}

/**************************************************************************/

typedef struct {
	GnmOutputXML  state;
	GnmCellRegion const *cr;
	GnmParsePos   pp;
} XMLCellCopyState;

static void
cb_xml_write_cell_region_cells (GnmCellCopy *cc, gconstpointer ignore,
				XMLCellCopyState *state)
{
	state->pp.eval.col = state->cr->base.col + cc->offset.col;
	state->pp.eval.row = state->cr->base.row + cc->offset.row;
	xml_write_cell_and_position (&state->state,
		cc->texpr, cc->val, &state->pp);
}

/**
 * gnm_cellregion_to_xml :
 * @cr  : the content to store.
 * @size: store the size of the buffer here.
 *
 * Caller is responsible for free-ing the result.
 * Returns NULL on error
 **/
GsfOutputMemory *
gnm_cellregion_to_xml (GnmCellRegion const *cr)
{
	XMLCellCopyState state;
	GnmStyleList *s_ptr;
	GSList       *ptr;
	GsfOutput    *buf = gsf_output_memory_new ();
	GnmLocale    *locale;

	g_return_val_if_fail (cr != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (cr->origin_sheet), NULL);

	state.state.wb_view = NULL;
	state.state.wb = NULL;
	state.state.sheet = NULL;
	state.state.output = gsf_xml_out_new (buf);
	state.state.convs = gnm_xml_io_conventions ();
	state.state.expr_map = g_hash_table_new (g_direct_hash, g_direct_equal);
	state.state.cell_str = g_string_new (NULL);

	locale = gnm_push_C_locale ();

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
			 workbook_date_conv (cr->origin_sheet->workbook));
	if (cr->not_as_contents)
		gsf_xml_out_add_bool (state.state.output, "NotAsContent", TRUE);

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
	}

	/* NOTE SNEAKY : ensure that sheet names have explicit workbooks */
	state.pp.wb    = NULL;
	state.pp.sheet = cr->origin_sheet;
	state.cr = cr;
	if (cr->cell_content != NULL) {
		gsf_xml_out_start_element (state.state.output, GNM "Cells");
		g_hash_table_foreach (cr->cell_content,
			(GHFunc) cb_xml_write_cell_region_cells, &state);
		gsf_xml_out_end_element (state.state.output); /* </Cells> */
	}

	xml_write_objects (&state.state, cr->objects);

	gsf_xml_out_end_element (state.state.output); /* </ClipboardRange> */

	gnm_pop_C_locale (locale);

	g_hash_table_destroy (state.state.expr_map);
	g_string_free (state.state.cell_str, TRUE);
	gnm_conventions_free (state.state.convs);
	g_object_unref (G_OBJECT (state.state.output));

	gsf_output_close (buf);

	return GSF_OUTPUT_MEMORY (buf);
}

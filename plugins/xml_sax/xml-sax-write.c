/* vim: set sw=8: */

/*
 * xml-sax-write.c : a test harness for a sax like xml export routine.
 *
 * Copyright (C) 2003 Jody Goldberg (jody@gnome.org)
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

/*****************************************************************************/

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <workbook-view.h>
#include <file.h>
#include <format.h>
#include <workbook.h>
#include <workbook-priv.h> /* Workbook::names */
#include <cell.h>
#include <sheet.h>
#include <sheet-view.h>
#include <sheet-style.h>
#include <summary.h>
#include <datetime.h>
#include <style-color.h>
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
#include <solver.h>
#include <sheet-filter.h>
#include <sheet-object-impl.h>
#include <print-info.h>
#include <print-info.h>
#include <xml-io.h>
#include <tools/scenarios.h>
#include <gnumeric-gconf.h>

#include <gsf/gsf-libxml.h>
#include <gsf/gsf-output-gzip.h>
#include <gsf/gsf-utils.h>
#include <locale.h>

typedef struct {
	IOContext 	*context;	/* The IOcontext managing things */
	WorkbookView const *wb_view;	/* View for the new workbook */
	Workbook const	   *wb;		/* The new workbook */
	Sheet const 	   *sheet;
	GnmExprConventions *exprconv;
	GHashTable	   *expr_map;

	GsfXMLOut *output;
} GnmOutputXML;

void	xml_sax_file_save (GnmFileSaver const *fs, IOContext *io_context,
			   WorkbookView const *wb_view, GsfOutput *output);

#define GMR "gmr:"

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
	gsf_xml_out_start_element (state->output, GMR "Attribute");
	/* backwards compatibility with 1.0.x which uses gtk-1.2 GTK_TYPE_BOOLEAN */
	gsf_xml_out_simple_element (state->output, GMR "type", "4");
	gsf_xml_out_simple_element (state->output, GMR "name", name);
	gsf_xml_out_simple_element (state->output, GMR "value", value);
	gsf_xml_out_end_element (state->output); /* </Attribute> */
}

static void
xml_write_version (GnmOutputXML *state)
{
	gsf_xml_out_start_element (state->output, GMR "Version");
	gsf_xml_out_add_int (state->output, "Epoch", GNM_VERSION_EPOCH);
	gsf_xml_out_add_int (state->output, "Major", GNM_VERSION_MAJOR);
	gsf_xml_out_add_int (state->output, "Minor", GNM_VERSION_MINOR);
	gsf_xml_out_add_cstr_unchecked (state->output, "Full", GNUMERIC_VERSION);
	gsf_xml_out_end_element (state->output); /* </Version> */
}

static void
xml_write_attributes (GnmOutputXML *state)
{
	gsf_xml_out_start_element (state->output, GMR "Attributes");
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
xml_write_summary (GnmOutputXML *state)
{
	SummaryInfo *summary_info = workbook_metadata (state->wb);
	GList *items, *ptr;
	SummaryItem *sit;

	if (summary_info == NULL)
		return;
	items = summary_info_as_list (summary_info);
	if (items == NULL)
		return;

	gsf_xml_out_start_element (state->output, GMR "Summary");
	for (ptr = items ; ptr != NULL ; ptr = ptr->next) {
		sit = items->data;
		if (sit == NULL)
			continue;
		gsf_xml_out_start_element (state->output, GMR "Item");
		gsf_xml_out_simple_element (state->output, GMR "name", sit->name);
		if (sit->type == SUMMARY_INT) {
			gsf_xml_out_simple_int_element (state->output,
				GMR "val-int", sit->v.i);
		} else {
			char *text = summary_item_as_text (sit);
			gsf_xml_out_simple_element (state->output, GMR "val-string", text);
			g_free (text);
		}
		gsf_xml_out_end_element (state->output);	/* </Item> */
	}
	gsf_xml_out_end_element (state->output); /* </Summary> */
	g_list_free (items);
}

static void
xml_write_conventions (GnmOutputXML *state)
{
	GnmDateConventions const *conv = workbook_date_conv (state->wb);
	if (conv->use_1904)
		gsf_xml_out_simple_element (state->output, GMR "DateConvention", "1904");
}

static void
xml_write_sheet_names (GnmOutputXML *state)
{
	int i, n = workbook_sheet_count (state->wb);
	Sheet *sheet;

	gsf_xml_out_start_element (state->output, GMR "SheetNameIndex");
	for (i = 0 ; i < n ; i++) {
		sheet = workbook_sheet_by_index (state->wb, i);
		gsf_xml_out_simple_element (state->output, GMR "SheetName",
			sheet->name_unquoted);
	}
	gsf_xml_out_end_element (state->output); /* </gmr:SheetNameIndex> */
}

static void
cb_xml_write_name (gpointer key, GnmNamedExpr *nexpr, GnmOutputXML *state)
{
	char *expr_str;

	g_return_if_fail (nexpr != NULL);

	gsf_xml_out_start_element (state->output, GMR "Name");
	gsf_xml_out_simple_element (state->output, "name",
		nexpr->name->str);
	expr_str = expr_name_as_string (nexpr, NULL, state->exprconv);
	gsf_xml_out_simple_element (state->output, "value", expr_str);
	g_free (expr_str);
	gsf_xml_out_simple_element (state->output, "position",
		cellpos_as_string (&nexpr->pos.eval));
	gsf_xml_out_end_element (state->output); /* </gmr:Name> */
}

static void
xml_write_named_expressions (GnmOutputXML *state, GnmNamedExprCollection *scope)
{
	if (scope != NULL) {
		gsf_xml_out_start_element (state->output, GMR "Names");
		g_hash_table_foreach (scope->names,
			(GHFunc) cb_xml_write_name, state);
		gsf_xml_out_end_element (state->output); /* </gmr:Names> */
	}
}

static void
xml_write_geometry (GnmOutputXML *state)
{
	gsf_xml_out_start_element (state->output, GMR "Geometry");
	gsf_xml_out_add_int (state->output, "Width", state->wb_view->preferred_width);
	gsf_xml_out_add_int (state->output, "Height", state->wb_view->preferred_height);
	gsf_xml_out_end_element (state->output); /* </gmr:Geometry> */
}

static void
xml_write_print_unit (GnmOutputXML *state, char const *name,
		      PrintUnit const *pu)
{
	gsf_xml_out_start_element (state->output, name);
	xml_out_add_points (state->output, "Points", pu->points);
	gsf_xml_out_add_cstr_unchecked (state->output, "PrefUnit", pu->desired_display->abbr);
	gsf_xml_out_end_element (state->output);
}

static void
xml_write_print_margin (GnmOutputXML *state, char const *name,
			double points)
{
	gsf_xml_out_start_element (state->output, name);
	xml_out_add_points (state->output, "Points", points);
	gsf_xml_out_add_cstr_unchecked (state->output, "PrefUnit", "Pt");
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
			range_name (&range->range));
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
xml_write_print_info (GnmOutputXML *state, PrintInformation *pi)
{
	guchar *paper_name;
	double header = 0, footer = 0, left = 0, right = 0;

	g_return_if_fail (pi != NULL);

	gsf_xml_out_start_element (state->output, GMR "PrintInformation");

	gsf_xml_out_start_element (state->output, GMR "Margins");
	print_info_get_margins (pi, &header, &footer, &left, &right);
	xml_write_print_unit (state, GMR "top",    &pi->margins.top);
	xml_write_print_unit (state, GMR "bottom", &pi->margins.bottom);
	xml_write_print_margin (state, GMR "left", left);
	xml_write_print_margin (state, GMR "right", right);
	xml_write_print_margin (state, GMR "header", header);
	xml_write_print_margin (state, GMR "footer", footer);
	gsf_xml_out_end_element (state->output);

	gsf_xml_out_start_element (state->output, GMR "Scale");
	if (pi->scaling.type == PERCENTAGE) {
		gsf_xml_out_add_cstr_unchecked  (state->output, "type", "percentage");
		gsf_xml_out_add_float  (state->output, "percentage", pi->scaling.percentage.x, -1);
	} else {
		gsf_xml_out_add_cstr_unchecked  (state->output, "type", "size_fit");
		gsf_xml_out_add_float  (state->output, "cols", pi->scaling.dim.cols, -1);
		gsf_xml_out_add_float  (state->output, "rows", pi->scaling.dim.rows, -1);
	}
	gsf_xml_out_end_element (state->output);

	gsf_xml_out_start_element (state->output, GMR "vcenter");
	gsf_xml_out_add_int  (state->output, "value", pi->center_vertically);
	gsf_xml_out_end_element (state->output);

	gsf_xml_out_start_element (state->output, GMR "hcenter");
	gsf_xml_out_add_int  (state->output, "value", pi->center_horizontally);
	gsf_xml_out_end_element (state->output);

	gsf_xml_out_start_element (state->output, GMR "grid");
	gsf_xml_out_add_int  (state->output, "value",    pi->print_grid_lines);
	gsf_xml_out_end_element (state->output);

	gsf_xml_out_start_element (state->output, GMR "even_if_only_styles");
	gsf_xml_out_add_int  (state->output, "value",    pi->print_even_if_only_styles);
	gsf_xml_out_end_element (state->output);

	gsf_xml_out_start_element (state->output, GMR "monochrome");
	gsf_xml_out_add_int  (state->output, "value",    pi->print_black_and_white);
	gsf_xml_out_end_element (state->output);

	gsf_xml_out_start_element (state->output, GMR "draft");
	gsf_xml_out_add_int  (state->output, "value",    pi->print_as_draft);
	gsf_xml_out_end_element (state->output);

	gsf_xml_out_start_element (state->output, GMR "titles");
	gsf_xml_out_add_int  (state->output, "value",    pi->print_titles);
	gsf_xml_out_end_element (state->output);

	xml_write_print_repeat_range (state, GMR "repeat_top", &pi->repeat_top);
	xml_write_print_repeat_range (state, GMR "repeat_left", &pi->repeat_left);

	gsf_xml_out_simple_element (state->output, GMR "order",
		(pi->print_order == PRINT_ORDER_DOWN_THEN_RIGHT) ? "d_then_r" : "r_then_d");
	gsf_xml_out_simple_element (state->output, GMR "orientation",
		     (print_info_get_orientation (pi) == PRINT_ORIENT_VERTICAL) ? "portrait" : "landscape");

	xml_write_print_hf (state, GMR "Header", pi->header);
	xml_write_print_hf (state, GMR "Footer", pi->footer);

	paper_name = gnome_print_config_get (pi->print_config,
					     (guchar *)GNOME_PRINT_KEY_PAPER_SIZE);
	if (paper_name) {
		gsf_xml_out_simple_element (state->output, GMR "paper", paper_name);
		g_free (paper_name);
	}

	gsf_xml_out_end_element (state->output);
}

static void
xml_write_gnmstyle (GnmOutputXML *state, GnmStyle const *style)
{
	static char const *border_names[] = {
		GMR "Top",
		GMR "Bottom",
		GMR "Left",
		GMR "Right",
		GMR "Diagonal",
		GMR "Rev-Diagonal"
	};
	GnmHLink   const *link;
	GnmValidation const *v;
	int i;

	gsf_xml_out_start_element (state->output, GMR "Style");

	if (mstyle_is_element_set (style, MSTYLE_ALIGN_H))
		gsf_xml_out_add_int (state->output, "HAlign", mstyle_get_align_h (style));
	if (mstyle_is_element_set (style, MSTYLE_ALIGN_V))
		gsf_xml_out_add_int (state->output, "VAlign", mstyle_get_align_v (style));
	if (mstyle_is_element_set (style, MSTYLE_WRAP_TEXT))
		gsf_xml_out_add_int (state->output, "WrapText", mstyle_get_wrap_text (style));
	if (mstyle_is_element_set (style, MSTYLE_SHRINK_TO_FIT))
		gsf_xml_out_add_int (state->output, "ShrinkToFit", mstyle_get_shrink_to_fit (style));
	if (mstyle_is_element_set (style, MSTYLE_ROTATION))
		gsf_xml_out_add_int (state->output, "Rotation", mstyle_get_rotation (style));
	if (mstyle_is_element_set (style, MSTYLE_PATTERN))
		gsf_xml_out_add_int (state->output, "Shade", mstyle_get_pattern (style));
	if (mstyle_is_element_set (style, MSTYLE_INDENT))
		gsf_xml_out_add_int (state->output, "Indent", mstyle_get_indent (style));
	if (mstyle_is_element_set (style, MSTYLE_CONTENT_LOCKED))
		gsf_xml_out_add_int (state->output, "Locked", mstyle_get_content_locked (style));
	if (mstyle_is_element_set (style, MSTYLE_CONTENT_HIDDEN))
		gsf_xml_out_add_int (state->output, "Hidden", mstyle_get_content_hidden (style));
	if (mstyle_is_element_set (style, MSTYLE_COLOR_FORE))
		gnm_xml_out_add_color (state->output, "Fore", mstyle_get_color (style, MSTYLE_COLOR_FORE));
	if (mstyle_is_element_set (style, MSTYLE_COLOR_BACK))
		gnm_xml_out_add_color (state->output, "Back", mstyle_get_color (style, MSTYLE_COLOR_BACK));
	if (mstyle_is_element_set (style, MSTYLE_COLOR_PATTERN))
		gnm_xml_out_add_color (state->output, "PatternColor", mstyle_get_color (style, MSTYLE_COLOR_PATTERN));
	if (mstyle_is_element_set (style, MSTYLE_FORMAT)) {
		char *fmt = style_format_as_XL (mstyle_get_format (style), FALSE);
		gsf_xml_out_add_cstr (state->output, "Format", fmt);
		g_free (fmt);
	}

	if (mstyle_is_element_set (style, MSTYLE_FONT_NAME) ||
	    mstyle_is_element_set (style, MSTYLE_FONT_SIZE) ||
	    mstyle_is_element_set (style, MSTYLE_FONT_BOLD) ||
	    mstyle_is_element_set (style, MSTYLE_FONT_ITALIC) ||
	    mstyle_is_element_set (style, MSTYLE_FONT_UNDERLINE) ||
	    mstyle_is_element_set (style, MSTYLE_FONT_STRIKETHROUGH)) {
		char const *fontname;

		gsf_xml_out_start_element (state->output, GMR "Font");

		if (mstyle_is_element_set (style, MSTYLE_FONT_SIZE))
			xml_out_add_points (state->output, "Unit", mstyle_get_font_size (style));
		if (mstyle_is_element_set (style, MSTYLE_FONT_BOLD))
			gsf_xml_out_add_int (state->output, "Bold", mstyle_get_font_bold (style));
		if (mstyle_is_element_set (style, MSTYLE_FONT_ITALIC))
			gsf_xml_out_add_int (state->output, "Italic", mstyle_get_font_italic (style));
		if (mstyle_is_element_set (style, MSTYLE_FONT_UNDERLINE))
			gsf_xml_out_add_int (state->output, "Underline", (int)mstyle_get_font_uline (style));
		if (mstyle_is_element_set (style, MSTYLE_FONT_STRIKETHROUGH))
			gsf_xml_out_add_int (state->output, "StrikeThrough", mstyle_get_font_strike (style));

		if (mstyle_is_element_set (style, MSTYLE_FONT_NAME))
			fontname = mstyle_get_font_name (style);
		else /* backwards compatibility */
			fontname = "Helvetica";

		gsf_xml_out_add_cstr (state->output, NULL, fontname);
		gsf_xml_out_end_element (state->output);
	}

	if ((link = mstyle_get_hlink (style)) != NULL) {
		gsf_xml_out_start_element (state->output, GMR "HyperLink");
		gsf_xml_out_add_cstr (state->output, "type", g_type_name (G_OBJECT_TYPE (link)));
		gsf_xml_out_add_cstr (state->output, "target", gnm_hlink_get_target (link));
		if (gnm_hlink_get_tip (link) != NULL)
			gsf_xml_out_add_cstr (state->output, "tip", gnm_hlink_get_tip (link));
		gsf_xml_out_end_element (state->output);
	}

	v = mstyle_get_validation (style);
	if (v != NULL) {
		GnmParsePos    pp;
		char	   *tmp;

		gsf_xml_out_start_element (state->output, GMR "Validation");
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
		if (v->expr[0] != NULL &&
		    (tmp = gnm_expr_as_string (v->expr[0], &pp, state->exprconv)) != NULL) {
			gsf_xml_out_simple_element (state->output, GMR "Expression0", tmp);
			g_free (tmp);
		}
		if (v->expr[1] != NULL &&
		    (tmp = gnm_expr_as_string (v->expr[1], &pp, state->exprconv)) != NULL) {
			gsf_xml_out_simple_element (state->output, GMR "Expression1", tmp);
			g_free (tmp);
		}
	}

	i = MSTYLE_BORDER_TOP;
	while (i <= MSTYLE_BORDER_DIAGONAL
	       && !mstyle_is_element_set (style, i)
	       && NULL == mstyle_get_border (style, i))
		i++;
	if (i <= MSTYLE_BORDER_DIAGONAL) {
		gsf_xml_out_start_element (state->output, GMR "StyleBorder");
		for (i = MSTYLE_BORDER_TOP; i <= MSTYLE_BORDER_DIAGONAL; i++) {
			GnmBorder const *border;
			if (mstyle_is_element_set (style, i) &&
			    NULL != (border = mstyle_get_border (style, i))) {
				StyleBorderType t = border->line_type;
				GnmColor *col   = border->color;
				gsf_xml_out_start_element (state->output, 
					border_names [i - MSTYLE_BORDER_TOP]);
				gsf_xml_out_add_int (state->output, "Style", t);
				if (t != STYLE_BORDER_NONE)
					gnm_xml_out_add_color (state->output, "Color", col);
				gsf_xml_out_end_element (state->output);
			}
		}
		gsf_xml_out_end_element (state->output);
	}
	gsf_xml_out_end_element (state->output);
}

static void
xml_write_style_region (GnmOutputXML *state, GnmStyleRegion const *region)
{
	gsf_xml_out_start_element (state->output, GMR "StyleRegion");
	xml_out_add_range (state->output, &region->range);
	if (region->style != NULL)
		xml_write_gnmstyle (state, region->style);
	gsf_xml_out_end_element (state->output);
}

static void
xml_write_styles (GnmOutputXML *state)
{
	GnmStyleList *ptr, *styles = sheet_style_get_list (state->sheet, NULL);
	if (styles != NULL) {
		gsf_xml_out_start_element (state->output, GMR "Styles");
		for (ptr = styles; ptr; ptr = ptr->next)
			xml_write_style_region (state, ptr->data);
		style_list_free (styles);
		gsf_xml_out_end_element (state->output);
	}
}

typedef struct {
	GnmOutputXML *state;
	gboolean is_column;
	ColRowInfo *previous;
	int rle_count;
} closure_write_colrow;

static gboolean
xml_write_colrow_info (ColRowInfo *info, closure_write_colrow *closure)
{
	ColRowInfo const *prev = closure->previous;
	GsfXMLOut *output = closure->state->output;

	closure->rle_count++;
	if (colrow_equal (prev, info))
		return FALSE;

	if (prev != NULL) {
		if (closure->is_column)
			gsf_xml_out_start_element (output, GMR "ColInfo");
		else
			gsf_xml_out_start_element (output, GMR "RowInfo");

		gsf_xml_out_add_int (output, "No", prev->pos);
		xml_out_add_points (output, "Unit", prev->size_pts);
		gsf_xml_out_add_int (output, "MarginA", prev->margin_a);
		gsf_xml_out_add_int (output, "MarginB", prev->margin_b);
		if (prev->hard_size)
			gsf_xml_out_add_bool (output, "HardSize", TRUE);
		if (!prev->visible)
			gsf_xml_out_add_bool (output, "Hidden", TRUE);
		if (prev->is_collapsed)
			gsf_xml_out_add_bool (output, "Collapsed", TRUE);
		if (prev->outline_level > 0)
			gsf_xml_out_add_bool (output, "OutlineLevel", prev->outline_level);

		if (closure->rle_count > 1)
			gsf_xml_out_add_int (output, "Count", closure->rle_count);
		gsf_xml_out_end_element (output);
	}

	closure->rle_count = 0;
	closure->previous = info;

	return FALSE;
}

static void
xml_write_cols_rows (GnmOutputXML *state)
{
	closure_write_colrow closure;
	gsf_xml_out_start_element (state->output, GMR "Cols");
	xml_out_add_points (state->output, "DefaultSizePts",
		sheet_col_get_default_size_pts (state->sheet));
	closure.state = state;
	closure.is_column = TRUE;
	closure.previous = NULL;
	closure.rle_count = 0;
	colrow_foreach (&state->sheet->cols, 0, SHEET_MAX_COLS-1,
		(ColRowHandler)&xml_write_colrow_info, &closure);
	xml_write_colrow_info (NULL, &closure); /* flush */
	gsf_xml_out_end_element (state->output); /* </gmr:Cols> */

	gsf_xml_out_start_element (state->output, GMR "Rows");
	xml_out_add_points (state->output, "DefaultSizePts",
		sheet_row_get_default_size_pts (state->sheet));
	closure.state = state;
	closure.is_column = FALSE;
	closure.previous = NULL;
	closure.rle_count = 0;
	colrow_foreach (&state->sheet->rows, 0, SHEET_MAX_ROWS-1,
		(ColRowHandler)&xml_write_colrow_info, &closure);
	xml_write_colrow_info (NULL, &closure); /* flush */
	gsf_xml_out_end_element (state->output); /* </gmr:Rows> */
}

static void
xml_write_selection_info (GnmOutputXML *state)
{
	GList *ptr, *copy;
	SheetView *sv = sheet_get_view (state->sheet, state->wb_view);

	gsf_xml_out_start_element (state->output, GMR "Selections");
	gsf_xml_out_add_int (state->output, "CursorCol", sv->edit_pos_real.col);
	gsf_xml_out_add_int (state->output, "CursorRow", sv->edit_pos_real.row);

	/* Insert the selections in REVERSE order */
	copy = g_list_copy (sv->selections);
	ptr = g_list_reverse (copy);
	for (; ptr != NULL ; ptr = ptr->next) {
		GnmRange const *r = ptr->data;
		gsf_xml_out_start_element (state->output, GMR "Selection");
		xml_out_add_range (state->output, r);
		gsf_xml_out_end_element (state->output); /* </gmr:Selection> */
	}
	g_list_free (copy);

	gsf_xml_out_end_element (state->output); /* </gmr:Selections> */
}

static int
natural_order_cmp (void const *a, void const *b)
{
	GnmCell const *ca = *(GnmCell const **)a ;
	GnmCell const *cb = *(GnmCell const **)b ;
	int diff = (ca->pos.row - cb->pos.row);
	if (diff != 0)
		return diff;
	return ca->pos.col - cb->pos.col;
}

static void
copy_hash_table_to_ptr_array (gpointer key, GnmCell *cell, gpointer user_data)
{
	if (!cell_is_empty (cell) || cell->base.expression != NULL)
		g_ptr_array_add (user_data, cell) ;
}

static void
xml_write_cell (GnmOutputXML *state, GnmCell const *cell, GnmParsePos const *pp)
{
	GnmExprArray const *ar;
	gboolean write_contents = TRUE;
	gboolean const is_shared_expr =
	    (cell_has_expr (cell) && gnm_expr_is_shared (cell->base.expression));

	/* Only the top left corner of an array needs to be saved (>= 0.53) */
	if (NULL != (ar = cell_is_array (cell)) && (ar->y != 0 || ar->x != 0))
		return; /* DOM version would write <Cell Col= Row=/> */

	gsf_xml_out_start_element (state->output, GMR "Cell");
	gsf_xml_out_add_int (state->output, "Col", pp->eval.col);
	gsf_xml_out_add_int (state->output, "Row", pp->eval.row);

	/* As of version 0.53 we save the ID of shared expressions */
	if (is_shared_expr) {
		gconstpointer const expr = cell->base.expression;
		gpointer id = g_hash_table_lookup (state->expr_map, expr);

		if (id == NULL) {
			id = GINT_TO_POINTER (g_hash_table_size (state->expr_map) + 1);
			g_hash_table_insert (state->expr_map, (gpointer)expr, id);
		} else if (ar == NULL)
			write_contents = FALSE;

		gsf_xml_out_add_int (state->output, "ExprID", GPOINTER_TO_INT (id));
	}

	/* As of version 0.53 we save the size of the array as attributes */
	/* As of version 0.57 the attributes are in the Cell not the Content */
	if (ar != NULL) {
	        gsf_xml_out_add_int (state->output, "Rows", ar->rows);
	        gsf_xml_out_add_int (state->output, "Cols", ar->cols);
	}

	if (write_contents) {
		GString *str;

		if (!cell_has_expr (cell)) {
			if (cell->value == NULL) {
				g_warning ("%s has no value ?", cellpos_as_string (&pp->eval));
				gsf_xml_out_end_element (state->output); /* </gmr:Cell> */
			}

			gsf_xml_out_add_int (state->output, "ValueType", cell->value->type);

			if (VALUE_FMT (cell->value) != NULL) {
				char *fmt = style_format_as_XL (VALUE_FMT (cell->value), FALSE);
				gsf_xml_out_add_cstr (state->output, "ValueFormat", fmt);
				g_free (fmt);
			}
		}

		str = g_string_sized_new (1000);
		if (cell_has_expr (cell)) {
			g_string_append_c (str, '=');
			gnm_expr_as_gstring (str, cell->base.expression, pp, state->exprconv);
		} else
			value_get_as_gstring (cell->value, str, state->exprconv);

		gsf_xml_out_add_cstr (state->output, NULL, str->str);
		g_string_free (str, TRUE);
	}
	gsf_xml_out_end_element (state->output); /* </gmr:Cell> */
}

static void
xml_write_cells (GnmOutputXML *state)
{
	size_t i;
	GPtrArray *natural = g_ptr_array_new ();
	GnmParsePos pp;
	GnmCell const *cell;

	gsf_xml_out_start_element (state->output, GMR "Cells");

	g_hash_table_foreach (state->sheet->cell_hash,
		(GHFunc) copy_hash_table_to_ptr_array, natural);
	qsort (&g_ptr_array_index (natural, 0), natural->len, sizeof (gpointer),
		natural_order_cmp);

	for (i = 0; i < natural->len; i++) {
		cell = g_ptr_array_index (natural, i);
		parse_pos_init_cell (&pp, cell);
		xml_write_cell (state, cell, &pp);
	}

	gsf_xml_out_end_element (state->output); /* </gmr:Cells> */

	g_ptr_array_free (natural, TRUE);
}

static void
xml_write_merged_regions (GnmOutputXML *state)
{
	GSList *ptr = state->sheet->list_merged;
	if (ptr == NULL)
		return;
	gsf_xml_out_start_element (state->output, GMR "MergedRegions");
	for (; ptr != NULL ; ptr = ptr->next)
		gsf_xml_out_simple_element (state->output,
			GMR "Merge", range_name (ptr->data));
	gsf_xml_out_end_element (state->output); /* </gmr:MergedRegions> */
}

static void
xml_write_sheet_layout (GnmOutputXML *state)
{
	SheetView const *sv = sheet_get_view (state->sheet, state->wb_view);

	gsf_xml_out_start_element (state->output, GMR "SheetLayout");
	gnm_xml_out_add_cellpos (state->output, "TopLeft", &sv->initial_top_left);

	if (sv_is_frozen (sv)) {
		gsf_xml_out_start_element (state->output, GMR "FreezePanes");
		gnm_xml_out_add_cellpos (state->output, "FrozenTopLeft", &sv->frozen_top_left);
		gnm_xml_out_add_cellpos (state->output, "UnfrozenTopLeft", &sv->unfrozen_top_left);
		gsf_xml_out_end_element (state->output); /* </gmr:FreezePanes> */
	}
	gsf_xml_out_end_element (state->output); /* </gmr:SheetLayout> */
}

static void
xml_write_filter_expr (GnmOutputXML *state,
		       GnmFilterCondition const *cond, unsigned i)
{
	static char const *filter_cond_name[] = { "eq", "gt", "lt", "gte", "lte", "ne" };
	static struct { char const *op, *valtype, *val; } filter_expr_attrs[] = {
		{ "Op0", "Value0", "ValueType0" },
		{ "Op1", "Value1", "ValueType1" }
	};

	GString *text = g_string_new (NULL);
	value_get_as_gstring (cond->value[i], text, state->exprconv);
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
	gsf_xml_out_start_element (state->output, GMR "Field");
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
		gsf_xml_out_add_int (state->output, "count", cond->count);
		break;
	}

	gsf_xml_out_end_element (state->output); /* </gmr:Field> */
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

	gsf_xml_out_start_element (state->output, GMR "Filters");

	for (ptr = state->sheet->filters; ptr != NULL ; ptr = ptr->next) {
		filter = ptr->data;
		gsf_xml_out_start_element (state->output, GMR "Filter");
		gsf_xml_out_add_cstr_unchecked (state->output, "Area",
			range_name (&filter->r));

		for (i = filter->fields->len ; i-- > 0 ; ) {
			cond = gnm_filter_get_condition (filter, i);
			if (cond != NULL && cond->op[0] != GNM_FILTER_UNUSED)
				xml_write_filter_field (state, cond, i);
		}

		gsf_xml_out_end_element (state->output); /* </gmr:Filter> */
	}

	gsf_xml_out_end_element (state->output); /* </gmr:Filters> */
}

static void
xml_write_solver (GnmOutputXML *state)
{
#ifdef ENABLE_SOLVER
        SolverParameters *param = state->sheet->solver_parameters;
	SolverConstraint const *c;
	int type;
	GSList *ptr;

	if (param == NULL)
		return;

	gsf_xml_out_start_element (state->output, GMR "Solver");

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
	gsf_xml_out_add_int (state->output, "NonNeg",
		param->options.assume_non_negative);
	gsf_xml_out_add_int (state->output, "Discr",
		param->options.assume_discrete);
	gsf_xml_out_add_int (state->output, "AutoScale",
		param->options.automatic_scaling);
	gsf_xml_out_add_int (state->output, "ShowIter",
		param->options.show_iter_results);
	gsf_xml_out_add_int (state->output, "AnswerR",
		param->options.answer_report);
	gsf_xml_out_add_int (state->output, "SensitivityR",
		param->options.sensitivity_report);
	gsf_xml_out_add_int (state->output, "LimitsR",
		param->options.limits_report);
	gsf_xml_out_add_int (state->output, "PerformR",
		param->options.performance_report);
	gsf_xml_out_add_int (state->output, "ProgramR",
		param->options.program_report);

	for (ptr = param->constraints; ptr != NULL ; ptr = ptr->next) {
	        c = ptr->data;

		gsf_xml_out_start_element (state->output, GMR "Constr");
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
		gsf_xml_out_end_element (state->output); /* </gmr:Constr> */
	}

	gsf_xml_out_end_element (state->output); /* </gmr:Solver> */
#endif
}

static void
xml_write_scenarios (GnmOutputXML *state)
{
	scenario_t const *s;
	GList   *ptr;
#if 0
	GString  *name;
	int       i, cols, rows;
#endif

	if (state->sheet->scenarios == NULL)
		return;

	gsf_xml_out_start_element (state->output, GMR "Scenarios");

	for (ptr = state->sheet->scenarios ; ptr != NULL ; ptr = ptr->next) {
	        s = (scenario_t const *)ptr->data;

		gsf_xml_out_start_element (state->output, GMR "Scenario");
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
			name = g_string_new (NULL);
			g_string_append_printf (name, "V%d", i);
			xml_node_set_value (scen, name->str,
					    s->changing_cells [i]);
			g_string_free (name, FALSE);
		}
#endif
	}

	gsf_xml_out_end_element (state->output); /* </gmr:Scenarios> */
}

static void
xml_write_objects (GnmOutputXML *state, Sheet const *sheet)
{
	GList *ptr;
	gboolean needs_container = TRUE;
	SheetObject	 *so;
	SheetObjectClass *klass;
	char buffer[4*(DBL_DIG+10)];
	char const *type_name;
	char *tmp;

	for (ptr = sheet->sheet_objects; ptr != NULL ; ptr = ptr->next) {
		so = ptr->data;
		klass = SHEET_OBJECT_CLASS (G_OBJECT_GET_CLASS (so));
		if (klass == NULL || klass->write_xml_sax == NULL)
			continue;

		if (needs_container) {
			needs_container = FALSE;
			gsf_xml_out_start_element (state->output, GMR "Objects");
		}

		/* A hook so that things can sometimes change names */
		type_name = klass->xml_export_name;
		if (type_name == NULL)
			type_name = G_OBJECT_TYPE_NAME (so);

		tmp = g_strconcat (GMR, type_name, NULL);
		gsf_xml_out_start_element (state->output, tmp);
		gsf_xml_out_add_cstr (state->output, "ObjectBound", range_name (&so->anchor.cell_bound));
		snprintf (buffer, sizeof (buffer), "%.3g %.3g %.3g %.3g",
			  so->anchor.offset [0], so->anchor.offset [1],
			  so->anchor.offset [2], so->anchor.offset [3]);
		gsf_xml_out_add_cstr (state->output, "ObjectOffset", buffer);
		snprintf (buffer, sizeof (buffer), "%d %d %d %d",
			  so->anchor.type [0], so->anchor.type [1],
			  so->anchor.type [2], so->anchor.type [3]);
		gsf_xml_out_add_cstr (state->output, "ObjectAnchorType", buffer);

		gsf_xml_out_add_int (state->output, "Direction",
			so->anchor.direction);

		(*klass->write_xml_sax) (so, state->output);

		gsf_xml_out_end_element (state->output); /* </gmr:{typename}> */
		g_free (tmp);
	}

	if (!needs_container)
		gsf_xml_out_end_element (state->output); /* </gmr:Objects> */
}

static void
xml_write_sheet (GnmOutputXML *state, Sheet const *sheet)
{
	state->sheet = sheet;
	gsf_xml_out_start_element (state->output, GMR "Sheet");

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

	if (sheet->tab_color != NULL)
		gnm_xml_out_add_color (state->output, "TabColor", sheet->tab_color);
	if (sheet->tab_text_color != NULL)
		gnm_xml_out_add_color (state->output, "TabTextColor", sheet->tab_text_color);

	gsf_xml_out_simple_element (state->output,
		GMR "Name", sheet->name_unquoted);
	gsf_xml_out_simple_int_element (state->output,
		GMR "MaxCol", sheet->cols.max_used);
	gsf_xml_out_simple_int_element (state->output,
		GMR "MaxRow", sheet->rows.max_used);
	gsf_xml_out_simple_float_element (state->output,
		GMR "Zoom", sheet->last_zoom_factor_used, 4);

	xml_write_named_expressions (state, sheet->names);
	xml_write_print_info (state, sheet->print_info);
	xml_write_styles (state);
	xml_write_cols_rows (state);
	xml_write_selection_info (state);
	xml_write_objects (state, sheet);
	xml_write_cells (state);

	xml_write_merged_regions (state);
	xml_write_sheet_layout (state);
	xml_write_sheet_filters (state);
	xml_write_solver (state);
	xml_write_scenarios (state);

	gsf_xml_out_end_element (state->output); /* </gmr:Sheet> */
	state->sheet = NULL;
}

static void
xml_write_sheets (GnmOutputXML *state)
{
	int i, n = workbook_sheet_count (state->wb);
	gsf_xml_out_start_element (state->output, GMR "Sheets");
	for (i = 0 ; i < n ; i++)
		xml_write_sheet (state, workbook_sheet_by_index (state->wb, i));
	gsf_xml_out_end_element (state->output); /* </gmr:Sheets> */
}

static void
xml_write_uidata (GnmOutputXML *state)
{
	gsf_xml_out_start_element (state->output, GMR "UIData");
	gsf_xml_out_add_int (state->output, "SelectedTab",
		wb_view_cur_sheet (state->wb_view)->index_in_wb);
	gsf_xml_out_end_element (state->output); /* </gmr:UIData> */
}

static void
xml_write_calculation (GnmOutputXML *state)
{
	gsf_xml_out_start_element (state->output, GMR "Calculation");
	gsf_xml_out_add_bool (state->output, 
		"ManualRecalc",		!state->wb->recalc_auto);
	gsf_xml_out_add_bool (state->output, 
		"EnableIteration",	state->wb->iteration.enabled);
	gsf_xml_out_add_int (state->output, 
		"MaxIterations",	state->wb->iteration.max_number);
	gsf_xml_out_add_float (state->output, 
		"IterationTolerance",	state->wb->iteration.tolerance, -1);
	gsf_xml_out_end_element (state->output); /* </gmr:Calculation> */
}

static GnmExprConventions *
xml_io_conventions (void)
{
	GnmExprConventions *res = gnm_expr_conventions_new ();

	res->decimal_sep_dot = TRUE;
	res->ref_parser = gnm_1_0_rangeref_parse;
	res->range_sep_colon = TRUE;
	res->sheet_sep_exclamation = TRUE;
	res->dots_in_names = TRUE;
	res->output_sheet_name_sep = "!";
	res->output_argument_sep = ",";
	res->output_array_col_sep = ",";
	res->output_translated = FALSE;
	res->unknown_function_handler = gnm_func_placeholder_factory;

	return res;
}

void
xml_sax_file_save (GnmFileSaver const *fs, IOContext *io_context,
		   WorkbookView const *wb_view, GsfOutput *output)
{
	GnmOutputXML state;
	char *old_num_locale, *old_monetary_locale;
	char const *extension = gsf_extension_pointer (gsf_output_name (output));
	GsfOutput *gzout = NULL;

	/* If the suffix is .xml disable compression */
	if (extension == NULL ||
	    g_ascii_strcasecmp (extension, "xml") != 0 ||
	    gnm_app_prefs->xml_compression_level != 0) {
		gzout  = gsf_output_gzip_new (output, NULL);
		output = gzout;
	}

	state.context	= io_context;
	state.wb_view	= wb_view;
	state.wb	= wb_view_workbook (wb_view);
	state.sheet	= NULL;
	state.output	= gsf_xml_out_new (output);
	state.exprconv	= xml_io_conventions ();
	state.expr_map  = g_hash_table_new (g_direct_hash, g_direct_equal);

	old_num_locale = g_strdup (gnm_setlocale (LC_NUMERIC, NULL));
	gnm_setlocale (LC_NUMERIC, "C");
	old_monetary_locale = g_strdup (gnm_setlocale (LC_MONETARY, NULL));
	gnm_setlocale (LC_MONETARY, "C");
	gnm_set_untranslated_bools ();

	gsf_xml_out_start_element (state.output, GMR "Workbook");
	gsf_xml_out_add_cstr_unchecked (state.output, "xmlns:gmr",
		"http://www.gnumeric.org/v10.dtd");
	gsf_xml_out_add_cstr_unchecked (state.output, "xmlns:xsi",
		"http://www.w3.org/2001/XMLSchema-instance");
	gsf_xml_out_add_cstr_unchecked (state.output, "xsi:schemaLocation",
		"http://www.gnumeric.org/v8.xsd");

	xml_write_version	    (&state);
	xml_write_attributes	    (&state);
	xml_write_summary	    (&state);
	xml_write_conventions	    (&state);
	xml_write_sheet_names	    (&state);
	xml_write_named_expressions (&state, state.wb->names);
	xml_write_geometry 	    (&state);
	xml_write_sheets 	    (&state);
	xml_write_uidata 	    (&state);
	xml_write_calculation 	    (&state);

	gsf_xml_out_end_element (state.output); /* </Workbook> */

	/* gnm_setlocale restores bools to locale translation */
	gnm_setlocale (LC_MONETARY, old_monetary_locale);
	g_free (old_monetary_locale);
	gnm_setlocale (LC_NUMERIC, old_num_locale);
	g_free (old_num_locale);

	g_hash_table_destroy (state.expr_map);
	gnm_expr_conventions_free (state.exprconv);
	g_object_unref (G_OBJECT (state.output));

	if (gzout) {
		gsf_output_close (gzout);
		g_object_unref (gzout);
	}
}

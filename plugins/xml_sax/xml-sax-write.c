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
#include <sheet.h>
#include <sheet-style.h>
#include <summary.h>
#include <datetime.h>
#include <style-color.h>
#include <expr-name.h>
#include <str.h>
#include <ranges.h>
#include <mstyle.h>
#include <style-border.h>
#include <validation.h>
#include <hlink.h>
#include <print-info.h>
#include <print-info.h>

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

	GsfXMLOut *output;
} GnmOutputXML;

void	xml_sax_file_save (GnmFileSaver const *fs, IOContext *io_context,
			   WorkbookView const *wb_view, GsfOutput *output);

/* Precision to use when saving point measures. */
#define POINT_SIZE_PRECISION 4

static void
xml_out_add_stylecolor (GsfXMLOut *xml, char const *id, StyleColor *sc)
{
	g_return_if_fail (sc != NULL);

	gsf_xml_out_add_color (xml, id,
		sc->color.red, sc->color.green, sc->color.blue);
}

static void
xml_out_add_range (GsfXMLOut *xml, Range const *r)
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
	gsf_xml_out_start_element (state->output, "gmr:Attribute");
	/* backwards compatibility with 1.0.x which uses gtk-1.2 GTK_TYPE_BOOLEAN */
	gsf_xml_out_simple_element (state->output, "gmr:type", "4");
	gsf_xml_out_simple_element (state->output, "gmr:name", name);
	gsf_xml_out_simple_element (state->output, "gmr:value", value);
	gsf_xml_out_end_element (state->output); /* </Attribute> */
}

static void
xml_write_attributes (GnmOutputXML *state)
{
	gsf_xml_out_start_element (state->output, "gmr:Attributes");
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

	gsf_xml_out_start_element (state->output, "gmr:Summary");
	for (ptr = items ; ptr != NULL ; ptr = ptr->next) {
		sit = items->data;
		if (sit == NULL)
			continue;
		gsf_xml_out_start_element (state->output, "gmr:Item");
		gsf_xml_out_simple_element (state->output, "gmr:name", sit->name);
		if (sit->type == SUMMARY_INT) {
			gsf_xml_out_simple_int_element (state->output,
				"gmr:val-int", sit->v.i);
		} else {
			char *text = summary_item_as_text (sit);
			gsf_xml_out_simple_element (state->output, "gmr:val-string", text);
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
		gsf_xml_out_simple_element (state->output, "gmr:DateConvention", "1904");
}

static void
xml_write_sheet_names (GnmOutputXML *state)
{
	int i, n = workbook_sheet_count (state->wb);
	Sheet *sheet;

	gsf_xml_out_start_element (state->output, "gmr:SheetNameIndex");
	for (i = 0 ; i < n ; i++) {
		sheet = workbook_sheet_by_index (state->wb, i);
		gsf_xml_out_simple_element (state->output, "gmr:SheetName",
			sheet->name_unquoted);
	}
	gsf_xml_out_end_element (state->output); /* </gmr:SheetNameIndex> */
}

static void
cb_xml_write_name (gpointer key, GnmNamedExpr *nexpr, GnmOutputXML *state)
{
	char *expr_str;

	g_return_if_fail (nexpr != NULL);

	gsf_xml_out_start_element (state->output, "gmr:Name");
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
		gsf_xml_out_start_element (state->output, "gmr:Names");
		g_hash_table_foreach (scope->names,
			(GHFunc) cb_xml_write_name, state);
		gsf_xml_out_end_element (state->output); /* </gmr:Names> */
	}
}

static void
xml_write_geometry (GnmOutputXML *state)
{
	gsf_xml_out_start_element (state->output, "gmr:Geometry");
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

	gsf_xml_out_start_element (state->output, "gmr:PrintInformation");

	gsf_xml_out_start_element (state->output, "gmr:Margins");
	print_info_get_margins (pi, &header, &footer, &left, &right);
	xml_write_print_unit (state, "gmr:top",    &pi->margins.top);
	xml_write_print_unit (state, "gmr:bottom", &pi->margins.bottom);
	xml_write_print_margin (state, "gmr:left", left);
	xml_write_print_margin (state, "gmr:right", right);
	xml_write_print_margin (state, "gmr:header", header);
	xml_write_print_margin (state, "gmr:footer", footer);
	gsf_xml_out_end_element (state->output);

	gsf_xml_out_start_element (state->output, "gmr:Scale");
	if (pi->scaling.type == PERCENTAGE) {
		gsf_xml_out_add_cstr_unchecked  (state->output, "type", "percentage");
		gsf_xml_out_add_float  (state->output, "percentage", pi->scaling.percentage.x, -1);
	} else {
		gsf_xml_out_add_cstr_unchecked  (state->output, "type", "size_fit");
		gsf_xml_out_add_float  (state->output, "cols", pi->scaling.dim.cols, -1);
		gsf_xml_out_add_float  (state->output, "rows", pi->scaling.dim.rows, -1);
	}
	gsf_xml_out_end_element (state->output);

	gsf_xml_out_start_element (state->output, "gmr:vcenter");
	gsf_xml_out_add_int  (state->output, "value", pi->center_vertically);
	gsf_xml_out_end_element (state->output);

	gsf_xml_out_start_element (state->output, "gmr:hcenter");
	gsf_xml_out_add_int  (state->output, "value", pi->center_horizontally);
	gsf_xml_out_end_element (state->output);

	gsf_xml_out_start_element (state->output, "gmr:grid");
	gsf_xml_out_add_int  (state->output, "value",    pi->print_grid_lines);
	gsf_xml_out_end_element (state->output);

	gsf_xml_out_start_element (state->output, "gmr:even_if_only_styles");
	gsf_xml_out_add_int  (state->output, "value",    pi->print_even_if_only_styles);
	gsf_xml_out_end_element (state->output);

	gsf_xml_out_start_element (state->output, "gmr:monochrome");
	gsf_xml_out_add_int  (state->output, "value",    pi->print_black_and_white);
	gsf_xml_out_end_element (state->output);

	gsf_xml_out_start_element (state->output, "gmr:draft");
	gsf_xml_out_add_int  (state->output, "value",    pi->print_as_draft);
	gsf_xml_out_end_element (state->output);

	gsf_xml_out_start_element (state->output, "gmr:titles");
	gsf_xml_out_add_int  (state->output, "value",    pi->print_titles);
	gsf_xml_out_end_element (state->output);

	xml_write_print_repeat_range (state, "gmr:repeat_top", &pi->repeat_top);
	xml_write_print_repeat_range (state, "gmr:repeat_left", &pi->repeat_left);

	gsf_xml_out_simple_element (state->output, "gmr:order",
		(pi->print_order == PRINT_ORDER_DOWN_THEN_RIGHT) ? "d_then_r" : "r_then_d");
	gsf_xml_out_simple_element (state->output, "gmr:orientation",
		     (pi->orientation == PRINT_ORIENT_VERTICAL) ? "portrait" : "landscape");

	xml_write_print_hf (state, "gmr:Header", pi->header);
	xml_write_print_hf (state, "gmr:Footer", pi->footer);

	paper_name = gnome_print_config_get (pi->print_config,
					     (guchar *)GNOME_PRINT_KEY_PAPER_SIZE);
	if (paper_name) {
		gsf_xml_out_simple_element (state->output, "gmr:paper", paper_name);
		g_free (paper_name);
	}

	gsf_xml_out_end_element (state->output);
}

static void
xml_write_style (GnmOutputXML *state, MStyle const *style)
{
	static char const *border_names[] = {
		"gmr:Top",
		"gmr:Bottom",
		"gmr:Left",
		"gmr:Right",
		"gmr:Diagonal",
		"gmr:Rev-Diagonal"
	};
	GnmHLink   const *link;
	Validation const *v;
	int i;

	gsf_xml_out_start_element (state->output, "gmr:Style");

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
		xml_out_add_stylecolor (state->output, "Fore", mstyle_get_color (style, MSTYLE_COLOR_FORE));
	if (mstyle_is_element_set (style, MSTYLE_COLOR_BACK))
		xml_out_add_stylecolor (state->output, "Back", mstyle_get_color (style, MSTYLE_COLOR_BACK));
	if (mstyle_is_element_set (style, MSTYLE_COLOR_PATTERN))
		xml_out_add_stylecolor (state->output, "PatternColor", mstyle_get_color (style, MSTYLE_COLOR_PATTERN));
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

		gsf_xml_out_start_element (state->output, "gmr:Font");

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
		gsf_xml_out_start_element (state->output, "gmr:HyperLink");
		gsf_xml_out_add_cstr (state->output, "type", g_type_name (G_OBJECT_TYPE (link)));
		gsf_xml_out_add_cstr (state->output, "target", gnm_hlink_get_target (link));
		if (gnm_hlink_get_tip (link) != NULL)
			gsf_xml_out_add_cstr (state->output, "tip", gnm_hlink_get_tip (link));
		gsf_xml_out_end_element (state->output);
	}

	v = mstyle_get_validation (style);
	if (v != NULL) {
		ParsePos    pp;
		char	   *tmp;

		gsf_xml_out_start_element (state->output, "gmr:Validation");
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
			gsf_xml_out_simple_element (state->output, "gmr:Expression0", tmp);
			g_free (tmp);
		}
		if (v->expr[1] != NULL &&
		    (tmp = gnm_expr_as_string (v->expr[1], &pp, state->exprconv)) != NULL) {
			gsf_xml_out_simple_element (state->output, "gmr:Expression1", tmp);
			g_free (tmp);
		}
	}

	i = MSTYLE_BORDER_TOP;
	while (i <= MSTYLE_BORDER_DIAGONAL
	       && !mstyle_is_element_set (style, i)
	       && NULL == mstyle_get_border (style, i))
		i++;
	if (i <= MSTYLE_BORDER_DIAGONAL) {
		gsf_xml_out_start_element (state->output, "gmr:StyleBorder");
		for (i = MSTYLE_BORDER_TOP; i <= MSTYLE_BORDER_DIAGONAL; i++) {
			StyleBorder const *border;
			if (mstyle_is_element_set (style, i) &&
			    NULL != (border = mstyle_get_border (style, i))) {
				StyleBorderType t = border->line_type;
				StyleColor *col   = border->color;
				gsf_xml_out_start_element (state->output, 
					border_names [i - MSTYLE_BORDER_TOP]);
				gsf_xml_out_add_int (state->output, "Style", t);
				if (t != STYLE_BORDER_NONE)
					xml_out_add_stylecolor (state->output, "Color", col);
				gsf_xml_out_end_element (state->output);
			}
		}
		gsf_xml_out_end_element (state->output);
	}
	gsf_xml_out_end_element (state->output);
}

static void
xml_write_style_region (GnmOutputXML *state, StyleRegion const *region)
{
	gsf_xml_out_start_element (state->output, "gmr:StyleRegion");
	xml_out_add_range (state->output, &region->range);
	if (region->style != NULL)
		xml_write_style (state, region->style);
	gsf_xml_out_end_element (state->output);
}

static void
xml_write_styles (GnmOutputXML *state)
{
	StyleList *ptr, *styles = sheet_style_get_list (state->sheet, NULL);
	if (styles != NULL) {
		gsf_xml_out_start_element (state->output, "gmr:Styles");
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
			gsf_xml_out_start_element (output, "gmr:ColInfo");
		else
			gsf_xml_out_start_element (output, "gmr:RowInfo");

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
	gsf_xml_out_start_element (state->output, "gmr:Cols");
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

	gsf_xml_out_start_element (state->output, "gmr:Rows");
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
xml_write_sheet (GnmOutputXML *state, Sheet const *sheet)
{
	state->sheet = sheet;
	gsf_xml_out_start_element (state->output, "gmr:Sheet");

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
		xml_out_add_stylecolor (state->output, "TabColor", sheet->tab_color);
	if (sheet->tab_text_color != NULL)
		xml_out_add_stylecolor (state->output, "TabTextColor", sheet->tab_text_color);

	gsf_xml_out_simple_element (state->output,
		"gmr:Name", sheet->name_unquoted);
	gsf_xml_out_simple_int_element (state->output,
		"gmr:MaxCol", sheet->cols.max_used);
	gsf_xml_out_simple_int_element (state->output,
		"gmr:MaxRow", sheet->rows.max_used);
	gsf_xml_out_simple_float_element (state->output,
		"gmr:Zoom", sheet->last_zoom_factor_used, 4);

	xml_write_named_expressions (state, sheet->names);
	xml_write_print_info (state, sheet->print_info);
	xml_write_styles (state);
	xml_write_cols_rows (state);

	gsf_xml_out_end_element (state->output); /* </gmr:Sheet> */
	state->sheet = NULL;
}

static void
xml_write_sheets (GnmOutputXML *state)
{
	int i, n = workbook_sheet_count (state->wb);
	gsf_xml_out_start_element (state->output, "gmr:Sheets");
	for (i = 0 ; i < n ; i++)
		xml_write_sheet (state, workbook_sheet_by_index (state->wb, i));
	gsf_xml_out_end_element (state->output); /* </gmr:Sheets> */
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
	if (extension == NULL || g_ascii_strcasecmp (extension, "xml") != 0) {
		gzout  = GSF_OUTPUT (gsf_output_gzip_new (output, NULL));
		output = gzout;
	}

	state.context	= io_context;
	state.wb_view	= wb_view;
	state.wb	= wb_view_workbook (wb_view);
	state.sheet	= NULL;
	state.output	= gsf_xml_out_new (output);
	state.exprconv	= xml_io_conventions ();

	old_num_locale = g_strdup (gnumeric_setlocale (LC_NUMERIC, NULL));
	gnumeric_setlocale (LC_NUMERIC, "C");
	old_monetary_locale = g_strdup (gnumeric_setlocale (LC_MONETARY, NULL));
	gnumeric_setlocale (LC_MONETARY, "C");

	gsf_xml_out_start_element (state.output, "gmr:Workbook");
	gsf_xml_out_add_cstr_unchecked (state.output, "xmlns:gmr",
		"http://www.gnumeric.org/v10.dtd");
	gsf_xml_out_add_cstr_unchecked (state.output, "xmlns:xsi",
		"http://www.w3.org/2001/XMLSchema-instance");
	gsf_xml_out_add_cstr_unchecked (state.output, "xsi:schemaLocation",
		"http://www.gnumeric.org/v8.xsd");

	xml_write_attributes	    (&state);
	xml_write_summary	    (&state);
	xml_write_conventions	    (&state);
	xml_write_sheet_names	    (&state);
	xml_write_named_expressions (&state, state.wb->names);
	xml_write_geometry 	    (&state);
	xml_write_sheets 	    (&state);

	gsf_xml_out_end_element (state.output); /* </Workbook> */

	gnumeric_setlocale (LC_MONETARY, old_monetary_locale);
	g_free (old_monetary_locale);
	gnumeric_setlocale (LC_NUMERIC, old_num_locale);
	g_free (old_num_locale);

	gnm_expr_conventions_free (state.exprconv);
	g_object_unref (G_OBJECT (state.output));

	if (gzout) {
		gsf_output_close (gzout);
		g_object_unref (gzout);
	}
}

/* vim: set sw=8: */

/*
 * openoffice-read.c : import open/star calc files
 *
 * Copyright (C) 2002 Jody Goldberg (jody@gnome.org)
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

#include <gnumeric-config.h>
#include <gnumeric.h>

#include <module-plugin-defs.h>
#include <workbook-view.h>
#include <workbook.h>
#include <sheet.h>
#include <cell.h>
#include <value.h>
#include <expr.h>
#include <parse-util.h>
#include <io-context.h>
#include <datetime.h>

#include <gnumeric-i18n.h>
#include <gsf/gsf-libxml.h>
#include <gsf/gsf-input.h>
#include <gsf/gsf-infile.h>
#include <gsf/gsf-infile-zip.h>

#include <string.h>

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

typedef struct {
	GsfXmlSAXState base;

	IOContext 	*context;	/* The IOcontext managing things */
	WorkbookView	*wb_view;	/* View for the new workbook */

	ParsePos 	pos;

	int 		 col_inc;
	gboolean 	 simple_content : 1;
	gboolean 	 error_content : 1;
} OOParseState;

static void oo_warning (OOParseState *state, char const *fmt, ...)
	G_GNUC_PRINTF (2, 3);

static void
oo_warning (OOParseState *state, char const *fmt, ...)
{
	char *msg;
	va_list args;

	va_start (args, fmt);
	msg = g_strdup_vprintf (fmt, args);
	va_end (args);

	if (IS_SHEET (state->pos.sheet)) {
		char *tmp;
		if (state->pos.eval.col >= 0 && state->pos.eval.row >= 0)
			tmp = g_strdup_printf ("%s!%s : %s",
				state->pos.sheet->name_quoted,
				cellpos_as_string (&state->pos.eval), msg);
		else
			tmp = g_strdup_printf ("%s : %s",
				state->pos.sheet->name_quoted, msg);
		g_free (msg);
		msg = tmp;
	}

	gnm_io_warning (state->context, msg);
	g_free (msg);
}

static gboolean
xml_sax_attr_bool (OOParseState *state,
		   xmlChar const * const *attrs, char const *name, gboolean *res)
{
	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (strcmp (attrs[0], name))
		return FALSE;

	*res = g_ascii_strcasecmp ((gchar *)attrs[1], "false") && strcmp (attrs[1], "0");

	return TRUE;
}

static gboolean
xml_sax_attr_int (OOParseState *state,
		  xmlChar const * const *attrs, char const *name, int *res)
{
	char *end;
	int tmp;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (strcmp (attrs[0], name))
		return FALSE;

	tmp = strtol ((gchar *)attrs[1], &end, 10);
	if (*end) {
		oo_warning (state, "Invalid attribute '%s', expected integer, received '%s'",
			    name, attrs[1]);
		return FALSE;
	}
	*res = tmp;
	return TRUE;
}

static gboolean
xml_sax_attr_float (OOParseState *state,
		    xmlChar const * const *attrs, char const *name, gnum_float *res)
{
	char *end;
	double tmp;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (strcmp (attrs[0], name))
		return FALSE;

	tmp = strtognum ((gchar *)attrs[1], &end);
	if (*end) {
		oo_warning (state, "Invalid attribute '%s', expected number, received '%s'",
			    name, attrs[1]);
		return FALSE;
	}
	*res = tmp;
	return TRUE;
}
/****************************************************************************/

static void
oo_table_start (GsfXmlSAXState *gsf_state, xmlChar const **attrs)
{
	/* <table:table table:name="Result" table:style-name="ta1"> */
	OOParseState *state = (OOParseState *)gsf_state;

	state->pos.eval.col = -1;
	state->pos.eval.row = -1;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (!strcmp (attrs[0], "table:name")) {
			state->pos.sheet = workbook_sheet_by_name (state->pos.wb, attrs[1]);
			if (NULL == state->pos.sheet) {
				state->pos.sheet = sheet_new (state->pos.wb, attrs[1]);
				workbook_sheet_attach (state->pos.wb, state->pos.sheet, NULL);
				puts (attrs[1]);
			} else {
				/* TODO : check the order */
			}
		}
}

static void
oo_col_start (GsfXmlSAXState *gsf_state, xmlChar const **attrs)
{
	/* <table:table-column table:style-name="co1" table:default-cell-style-name="ce1"/> */
	/* <table:table-column table:style-name="co2" table:number-columns-repeated="255" table:default-cell-style-name="Default"/> */
}

static void
oo_row_start (GsfXmlSAXState *gsf_state, xmlChar const **attrs)
{
	/* <table:table-row table:style-name="ro1"> */
	OOParseState *state = (OOParseState *)gsf_state;
	int      repeat_count;
	gboolean repeat_flag = FALSE;

	state->pos.eval.row++;
	state->pos.eval.col = 0;

	g_return_if_fail (state->pos.eval.row < SHEET_MAX_ROWS);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (xml_sax_attr_int (state, attrs, "table:number-rows-repeated", &repeat_count))
			repeat_flag = TRUE;
	}
	if (repeat_flag)
		state->pos.eval.row += repeat_count -1;
}
static char const *
oo_cellref_parse (CellRef *ref, char const *start, ParsePos const *pp)
{
	char const *tmp1, *tmp2, *ptr = start;
	/* sheet name can not contain '.' */
	if (*ptr != '.') {
		char *name;
		if (*ptr == '$') /* ignore abs vs rel sheet name */
			ptr++;
		tmp1 = strchr (ptr, '.');
		if (tmp1 == NULL)
			return start;
		name = g_alloca (tmp1-ptr+1);
		strncpy (name, ptr, tmp1-ptr);
		name[tmp1-ptr] = 0;
		ptr = tmp1+1;

		/* OpenCalc does not pre-declare its sheets, but it does have a
		 * nice unambiguous format.  So if we find a name that has not
		 * been added yet add it.
		 * TODO : worry about order
		 */
		ref->sheet = workbook_sheet_by_name (pp->wb, name);
		if (ref->sheet == NULL) {
			printf ("--> %s\n", name);
			ref->sheet = sheet_new (pp->wb, name);
			workbook_sheet_attach (pp->wb, ref->sheet, NULL);
		}
	} else {
		ptr++; /* local ref */
		ref->sheet = NULL;
	}

	tmp1 = col_parse (ptr, &ref->col, &ref->col_relative);
	if (tmp1 == ptr)
		return start;
	tmp2 = row_parse (tmp1, &ref->row, &ref->row_relative);
	if (tmp2 == tmp1)
		return start;

	if (ref->col_relative)
		ref->col -= pp->eval.col;
	if (ref->row_relative)
		ref->row -= pp->eval.row;
	return tmp2;
}

static char const *
oo_rangeref_parse (RangeRef *ref, char const *start, ParsePos const *pp)
{
	char const *ptr;

	g_return_val_if_fail (start != NULL, start);
	g_return_val_if_fail (pp != NULL, start);

	if (*start != '[')
		return start;
	ptr = oo_cellref_parse (&ref->a, start+1, pp);
	if (*ptr == ':')
		ptr = oo_cellref_parse (&ref->b, ptr+1, pp);
	else
		ref->b = ref->a;

	if (*ptr == ']')
		return ptr + 1;
	return start;
}

static void
oo_cell_start (GsfXmlSAXState *gsf_state, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)gsf_state;
	GnmExpr const	*expr = NULL;
	Value		*val = NULL;
	gboolean	 bool_val;
	gnum_float	 float_val;
	int array_cols = -1, array_rows = -1;

	state->col_inc = 1;
	state->error_content = FALSE;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (xml_sax_attr_int (state, attrs, "table:number-columns-repeated", &state->col_inc))
			;
		else if (!strcmp (attrs[0], "table:formula")) {
			char const *expr_string;

			if (attrs[1] == NULL) {
				oo_warning (state, _("Missing expression"));
				continue;
			}
			expr_string = gnm_expr_char_start_p (attrs[1]);
			if (expr_string == NULL)
				oo_warning (state, _("Expression '%s' does not start with a recognized character"), attrs[1]);
			else if (*expr_string == '\0')
				/* Ick.  They seem to store error cells as
				 * having value date with expr : '=' and the
				 * message in the content.
				 */
				state->error_content = TRUE;
			else {
				ParseError  perr;
				parse_error_init (&perr);
				expr = gnm_expr_parse_str (expr_string, &state->pos,
					GNM_EXPR_PARSE_USE_OPENCALC_CONVENTIONS |
					GNM_EXPR_PARSE_CREATE_PLACEHOLDER_FOR_UNKNOWN_FUNC,
					&oo_rangeref_parse,
					&perr);
				if (expr == NULL) {
					oo_warning (state, _("Unable to parse '%s' because '%s'"),
						    attrs[1], perr.message);
					parse_error_free (&perr);
				}
			}
		}
		else if (xml_sax_attr_bool (state, attrs, "table:boolean-value", &bool_val))
			val = value_new_bool (bool_val);
		else if (!strcmp (attrs[0], "table:date-value")) {
			unsigned y, m, d;
			if (3 == sscanf (attrs[1], "%u-%u-%u", &y, &m, &d)) {
				GDate date;
				g_date_set_dmy (&date, d, m, y);
				if (g_date_valid (&date))
					val = value_new_int (datetime_g_to_serial (&date));
			}
		} else if (!strcmp (attrs[0], "table:string-value"))
			val = value_new_string (attrs[1]);
		else if (xml_sax_attr_float (state, attrs, "table:value", &float_val))
			val = value_new_float (float_val);
		else if (xml_sax_attr_int (state, attrs, "table:number-matrix-columns-spanned", &array_cols))
			;
		else if (xml_sax_attr_int (state, attrs, "table:number-matrix-rows-spanned", &array_rows))
			;
	}

	state->simple_content = FALSE;
	if (expr != NULL) {
		Cell *cell = sheet_cell_fetch (state->pos.sheet,
			state->pos.eval.col, state->pos.eval.row);

		if (array_cols > 0 || array_rows > 0) {
			if (array_cols < 0) {
				array_cols = 1;
				oo_warning (state, _("Invalid array expression does no specify number of columns"));
			} else if (array_rows < 0) {
				array_rows = 1;
				oo_warning (state, _("Invalid array expression does no specify number of rows"));
			}
			cell_set_array_formula (state->pos.sheet,
				state->pos.eval.col, state->pos.eval.row,
				state->pos.eval.col + array_cols-1,
				state->pos.eval.row + array_rows-1,
				expr);
			if (val != NULL)
				cell_assign_value (cell, val);
		} else {
			if (val != NULL)
				cell_set_expr_and_value (cell, expr, val, TRUE);
			else
				cell_set_expr (cell, expr);
			gnm_expr_unref (expr);
		}
	} else if (val != NULL) {
		Cell *cell = sheet_cell_fetch (state->pos.sheet,
			state->pos.eval.col, state->pos.eval.row);

		/* has cell previously been initialized as part of an array */
		if (cell_is_partial_array (cell))
			cell_assign_value (cell, val);
		else
			cell_set_value (cell, val);
	} else if (!state->error_content)
		/* store the content as a string */
		state->simple_content = TRUE;
}

static void
oo_cell_end (GsfXmlSAXState *gsf_state)
{
	OOParseState *state = (OOParseState *)gsf_state;
	state->pos.eval.col += state->col_inc;
}

static void
oo_cell_content_end (GsfXmlSAXState *gsf_state)
{
	/* <text:p>EQUAL</text:p> */
	OOParseState *state = (OOParseState *)gsf_state;

	if (state->simple_content || state->error_content) {
		Cell *cell = sheet_cell_fetch (state->pos.sheet, state->pos.eval.col, state->pos.eval.row);
		Value *v;

		if (state->simple_content)
			v = value_new_string (state->base.content->str);
		else
			v = value_new_error (NULL, state->base.content->str);
		cell_set_value (cell, v);
	}
}

static GsfXmlSAXNode opencalc_dtd[] = {
GSF_XML_SAX_NODE (START, START, NULL, FALSE, NULL, NULL, 0),
GSF_XML_SAX_NODE (START, OFFICE, "office:document-content", FALSE, NULL, NULL, 0),
  GSF_XML_SAX_NODE (OFFICE, SCRIPT, "office:script", FALSE, NULL, NULL, 0),
  /* <office:script/> */

  GSF_XML_SAX_NODE (OFFICE, OFFICE_FONTS, "office:font-decls", FALSE, NULL, NULL, 0),
  /* <office:font-decls> */

    GSF_XML_SAX_NODE (OFFICE_FONTS, FONT_DECL, "style:font-decl", FALSE, NULL, NULL, 0),
    /* <style:font-decl style:name="Arial" fo:font-family="Arial"/> */

  GSF_XML_SAX_NODE (OFFICE, OFFICE_STYLES, "office:automatic-styles", FALSE, NULL, NULL, 0),
  /* <office:automatic-styles> */

    GSF_XML_SAX_NODE (OFFICE_STYLES, STYLE, "style:style", FALSE, NULL, NULL, 0),
      /* <style:style style:name="co1" style:family="table-column"> */

      GSF_XML_SAX_NODE (STYLE, STYLE_PROP, "style:properties", FALSE, NULL, NULL, 0),
	/* <style:properties fo:break-before="auto" style:column-width="0.9138inch"/> */

    GSF_XML_SAX_NODE (OFFICE_STYLES, NUMBER_STYLE, "number:number-style", FALSE, NULL, NULL, 0),
    /* <number:number-style style:name="N1" style:family="data-style"> */

      GSF_XML_SAX_NODE (NUMBER_STYLE, NUMBER_STYLE_PROP, "number:number", FALSE, NULL, NULL, 0),
      /* <number:number number:decimal-places="0" number:min-integer-digits="1"/> */

      GSF_XML_SAX_NODE (NUMBER_STYLE, NUMBER_SCI_STYLE_PROP, "number:scientific-number", FALSE, NULL, NULL, 0),
      /* <number:scientific-number number:decimal-places="2" number:min-integer-digits="1" number:min-exponent-digits="2"/> */

    GSF_XML_SAX_NODE (OFFICE_STYLES, DATE_STYLE, "number:date-style", FALSE, NULL, NULL, 0),
    /* <number:date-style style:name="N32" style:family="data-style" number:automatic-order="true"> */

      GSF_XML_SAX_NODE (DATE_STYLE, DATE_YEAR, "number:year", FALSE, NULL, NULL, 0),
      GSF_XML_SAX_NODE (DATE_STYLE, DATE_MONTH, "number:month", FALSE, NULL, NULL, 0),
      GSF_XML_SAX_NODE (DATE_STYLE, DATE_DAY, "number:day", FALSE, NULL, NULL, 0),
      GSF_XML_SAX_NODE (DATE_STYLE, DATE_TEXT, "number:text", FALSE, NULL, NULL, 0),


  GSF_XML_SAX_NODE (OFFICE, OFFICE_BODY, "office:body", FALSE, NULL, NULL, 0),
    GSF_XML_SAX_NODE (OFFICE_BODY, TABLE_CALC_SETTINGS, "table:calculation-settings", FALSE, NULL, NULL, 0),
    /* <table:calculation-settings table:case-sensitive="false" table:use-regular-expressions="false"/> */

    GSF_XML_SAX_NODE (OFFICE_BODY, TABLE, "table:table", FALSE, &oo_table_start, NULL, 0),
      GSF_XML_SAX_NODE (TABLE, TABLE_COL, "table:table-column", FALSE, &oo_col_start, NULL, 0),
      GSF_XML_SAX_NODE (TABLE, TABLE_ROW, "table:table-row", FALSE, &oo_row_start, NULL, 0),
	GSF_XML_SAX_NODE (TABLE_ROW, TABLE_CELL, "table:table-cell", FALSE, &oo_cell_start, &oo_cell_end, 0),
	  GSF_XML_SAX_NODE (TABLE_CELL, CELL_TEXT, "text:p", TRUE, NULL, &oo_cell_content_end, 0),
  { NULL }
};

void
openoffice_file_open (GnumFileOpener const *fo, IOContext *io_context,
		      WorkbookView *wb_view, GsfInput *input)
{
	OOParseState state;
	GsfInput *content = NULL;
	GError   *err = NULL;
	GsfInfile *zip;
	int i;

	g_return_if_fail (IS_WORKBOOK_VIEW (wb_view));
	g_return_if_fail (GSF_IS_INPUT (input));

	zip = gsf_infile_zip_new (input, &err);
	if (zip == NULL) {
		g_return_if_fail (err != NULL);
		gnumeric_io_error_read (io_context, err->message);
		g_error_free (err);
		return;
	}

	content = gsf_infile_child_by_name (zip, "content.xml");
	if (content == NULL) {
		gnumeric_io_error_read (io_context,
			 _("No stream named content.xml found."));
		g_object_unref (G_OBJECT (zip));
		return;
	}

	/* init */
	state.context = io_context;
	state.wb_view = wb_view;
	state.pos.wb	= wb_view_workbook (wb_view);
	state.pos.sheet = NULL;
	state.pos.eval.col	= -1;
	state.pos.eval.row	= -1;

	state.base.root = opencalc_dtd;
	if (!gsf_xmlSAX_parse (content, &state.base))
		gnumeric_io_error_string (io_context, _("XML document not well formed!"));
	else
		workbook_queue_all_recalc (state.pos.wb);

	g_object_unref (G_OBJECT (content));
	g_object_unref (G_OBJECT (zip));

	i = workbook_sheet_count (state.pos.wb);
	while (i-- > 0)
		sheet_flag_recompute_spans (workbook_sheet_by_index (state.pos.wb, i));
}

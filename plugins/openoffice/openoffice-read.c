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
#include <parse-util.h>

#include <gnumeric-i18n.h>
#include <gsf/gsf-libxml.h>
#include <gsf/gsf-input.h>
#include <gsf/gsf-infile.h>
#include <gsf/gsf-infile-zip.h>

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

static gboolean
xml_sax_attr_double (xmlChar const * const *attrs, char const *name, double * res)
{
	char *end;
	double tmp;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (strcmp (attrs[0], name))
		return FALSE;

	tmp = g_strtod ((gchar *)attrs[1], &end);
	if (*end) {
		g_warning ("Invalid attribute '%s', expected double, received '%s'",
			   name, attrs[1]);
		return FALSE;
	}
	*res = tmp;
	return TRUE;
}
static gboolean
xml_sax_attr_bool (xmlChar const * const *attrs, char const *name, gboolean *res)
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
xml_sax_attr_int (xmlChar const * const *attrs, char const *name, int *res)
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
		g_warning ("Invalid attribute '%s', expected integer, received '%s'",
			   name, attrs[1]);
		return FALSE;
	}
	*res = tmp;
	return TRUE;
}

/****************************************************************************/

typedef struct {
	GsfXmlSAXState base;

	IOContext 	*context;	/* The IOcontext managing things */
	WorkbookView	*wb_view;	/* View for the new workbook */
	Workbook	*wb;		/* The new workbook */

	Sheet		*sheet;
	int		 col, row;	/* blah position is implicit */
	int 		 col_inc;
	gboolean 	 simple_content : 1;
	gboolean 	 error_content : 1;
} OOParseState;

static void
oo_table_start (GsfXmlSAXState *gsf_state, xmlChar const **attrs)
{
	/* <table:table table:name="Result" table:style-name="ta1"> */
	OOParseState *state = (OOParseState *)gsf_state;

	state->col = -1;
	state->row = -1;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (!strcmp (attrs[0], "table:name")) {
			state->sheet = sheet_new (state->wb, attrs[1]);
			workbook_sheet_attach (state->wb, state->sheet, NULL);

			puts (attrs[1]);
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

	state->row++;
	state->col = 0;

	g_return_if_fail (state->row < SHEET_MAX_ROWS);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (xml_sax_attr_int (attrs, "table:number-rows-repeated", &repeat_count))
			repeat_flag = TRUE;
	}
	if (repeat_flag)
		state->row += repeat_count -1;
}

static void
oo_cell_start (GsfXmlSAXState *gsf_state, xmlChar const **attrs)
{
	/* <table:table-cell> */
        /* <table:table-cell table:formula="=(1=1)" table:value-type="boolean" table:boolean-value="true"> */
	OOParseState *state = (OOParseState *)gsf_state;
	GnmExpr const *expr = NULL;
	Value *val = NULL;

	state->col_inc = 1;
	state->error_content = FALSE;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (xml_sax_attr_int (attrs, "table:number-columns-repeated", &state->col_inc))
			;
		else if (!strcmp (attrs[0], "table:formula")) {
			/* Ick.  They seem to store error cells as having value date
			 * with expr : '=' and the message in the content.
			 */
			if (!strcmp (attrs[1], "="))
				state->error_content = TRUE;
		} else if (!strcmp (attrs[0], "table:value-type"))
			state->simple_content = FALSE;
		else if (!strcmp (attrs[0], "table:boolean-value"))
			;
		else if (!strcmp (attrs[0], "table:date-value"))
			;
		else if (!strcmp (attrs[0], "table:string-value"))
			;
		else if (!strcmp (attrs[0], "table:value"))
			;
	}

	if (expr != NULL) {
	} else if (val != NULL) {
	} else if (!state->error_content)
		/* store the content as a string */
		state->simple_content = TRUE;
}

static void
oo_cell_end (GsfXmlSAXState *gsf_state)
{
	OOParseState *state = (OOParseState *)gsf_state;
	state->col += state->col_inc;
}

static void
oo_cell_content_end (GsfXmlSAXState *gsf_state)
{
	/* <text:p>EQUAL</text:p> */
	OOParseState *state = (OOParseState *)gsf_state;

	if (state->simple_content) {
		Cell *cell = sheet_cell_fetch (state->sheet, state->col, state->row);
		cell_set_value (cell, value_new_string (state->base.content->str));
	} else if (state->error_content) {
		Cell *cell = sheet_cell_fetch (state->sheet, state->col, state->row);
		cell_set_value (cell, value_new_error (NULL, state->base.content->str));
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
	state.wb = wb_view_workbook (wb_view);
	state.sheet = NULL;
	state.col = -1;
	state.row = -1;

	state.base.root = opencalc_dtd;
	if (!gsf_xmlSAX_parse (content, &state.base))
		gnumeric_io_error_string (io_context, _("XML document not well formed!"));
	else
		workbook_queue_all_recalc (state.wb);

	g_object_unref (G_OBJECT (content));
	g_object_unref (G_OBJECT (zip));

	i = workbook_sheet_count (state.wb);
	while (i-- > 0)
		sheet_flag_recompute_spans (workbook_sheet_by_index (state.wb, i));
}

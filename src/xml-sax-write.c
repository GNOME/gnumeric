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

#include <gnumeric.h>
#include <workbook-view.h>
#include <file.h>
#include <format.h>
#include <workbook.h>
#include <workbook-priv.h> /* Workbook::names */
#include <sheet.h>
#include <summary.h>
#include <datetime.h>
#include <expr-name.h>
#include <str.h>

#include <gsf/gsf-libxml.h>
#include <gsf/gsf-output-gzip.h>
#include <gsf/gsf-utils.h>
#include <locale.h>

typedef struct {
	IOContext 	*context;	/* The IOcontext managing things */
	WorkbookView const *wb_view;	/* View for the new workbook */
	Workbook const	   *wb;		/* The new workbook */
	GnmExprConventions *exprconv;

	GsfOutputXML *output;
} GnmOutputXML;

static void
xml_write_attribute (GnmOutputXML *state, char const *name, char const *value)
{
	gsf_output_xml_start_element (state->output, "Attribute");
	/* backwards compatibility with 1.0.x which uses gtk-1.2 GTK_TYPE_BOOLEAN */
	gsf_output_xml_simple_element (state->output, "type", "4");
	gsf_output_xml_simple_element (state->output, "name", name);
	gsf_output_xml_simple_element (state->output, "value", value);
	gsf_output_xml_end_element (state->output); /* </Attribute> */
}

static void
xml_write_attributes (GnmOutputXML *state)
{
	gsf_output_xml_start_element (state->output, "Attributes");
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
	gsf_output_xml_end_element (state->output); /* </Attributes> */
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

	gsf_output_xml_start_element (state->output, "Summary");
	for (ptr = items ; ptr != NULL ; ptr = ptr->next) {
		sit = items->data;
		if (sit == NULL)
			continue;
		gsf_output_xml_start_element (state->output, "Item");
		gsf_output_xml_simple_element (state->output, "name", sit->name);
		if (sit->type == SUMMARY_INT) {
			gsf_output_xml_start_element (state->output, "val-int");
			gsf_output_xml_add_attr_int (state->output, NULL, sit->v.i);
			gsf_output_xml_end_element (state->output); /* </val-int> */
		} else {
			char *text = summary_item_as_text (sit);
			gsf_output_xml_simple_element (state->output, "val-string", text);
			g_free (text);
		}
		gsf_output_xml_end_element (state->output);	/* </Item> */
	}
	gsf_output_xml_end_element (state->output); /* </Summary> */
	g_list_free (items);
}

static void
xml_write_conventions (GnmOutputXML *state)
{
	GnmDateConventions const *conv = workbook_date_conv (state->wb);
	gsf_output_xml_start_element (state->output, "Conventions");
	if (conv->use_1904)
		gsf_output_xml_simple_element (state->output, "DateOrigin", "1904");
	gsf_output_xml_end_element (state->output); /* </Conventions> */
}

static void
xml_write_sheet_names (GnmOutputXML *state)
{
	int i, n = workbook_sheet_count (state->wb);
	Sheet *sheet;

	gsf_output_xml_start_element (state->output, "SheetNameIndex");
	for (i = 0 ; i < n ; i++) {
		sheet = workbook_sheet_by_index (state->wb, i);
		gsf_output_xml_simple_element (state->output, "SheetName",
			sheet->name_unquoted);
	}
	gsf_output_xml_end_element (state->output); /* </SheetNameIndex> */
}

static void
cb_xml_write_name (gpointer key, GnmNamedExpr *nexpr, GnmOutputXML *state)
{
	char *expr_str;

	g_return_if_fail (nexpr != NULL);

	gsf_output_xml_start_element (state->output, "Name");
	gsf_output_xml_simple_element (state->output, "name",
		nexpr->name->str);
	expr_str = expr_name_as_string (nexpr, NULL, state->exprconv);
	gsf_output_xml_simple_element (state->output, "value", expr_str);
	g_free (expr_str);
	gsf_output_xml_simple_element (state->output, "position",
		cellpos_as_string (&nexpr->pos.eval));
	gsf_output_xml_end_element (state->output); /* </Name> */
}

static void
xml_write_named_expressions (GnmOutputXML *state, GnmNamedExprCollection *scope)
{
	if (scope != NULL) {
		gsf_output_xml_start_element (state->output, "Names");
		g_hash_table_foreach (scope->names,
			(GHFunc) cb_xml_write_name, state);
		gsf_output_xml_end_element (state->output); /* </Names> */
	}
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
	    g_ascii_strcasecmp (extension, "xml") != 0) {
		gzout  = GSF_OUTPUT (gsf_output_gzip_new (output, NULL));
		g_object_unref (output);
		output = gzout;
	}

	state.context	= io_context;
	state.wb_view	= wb_view;
	state.wb	= wb_view_workbook (wb_view);
	state.output	= gsf_output_xml_new (output);
	state.exprconv	= xml_io_conventions ();

	old_num_locale = g_strdup (gnumeric_setlocale (LC_NUMERIC, NULL));
	gnumeric_setlocale (LC_NUMERIC, "C");
	old_monetary_locale = g_strdup (gnumeric_setlocale (LC_MONETARY, NULL));
	gnumeric_setlocale (LC_MONETARY, "C");

	gsf_output_xml_set_namespace (state.output, "gmr");

	gsf_output_xml_start_element (state.output, "Workbook");
	gsf_output_xml_add_attr_cstr (state.output, "xmlns:gmr",
		"http://www.gnumeric.org/v10.dtd");
	gsf_output_xml_add_attr_cstr (state.output, "xmlns:xsi",
		"http://www.w3.org/2001/XMLSchema-instance");
	gsf_output_xml_add_attr_cstr (state.output, "xsi:schemaLocation",
		"http://www.gnumeric.org/v8.xsd");

	xml_write_attributes	    (&state);
	xml_write_summary	    (&state);
	xml_write_conventions	    (&state);
	xml_write_sheet_names	    (&state);
	xml_write_named_expressions (&state, state.wb->names);

	gsf_output_xml_end_element (state.output); /* </Workbook> */

	gnumeric_setlocale (LC_MONETARY, old_monetary_locale);
	g_free (old_monetary_locale);
	gnumeric_setlocale (LC_NUMERIC, old_num_locale);
	g_free (old_num_locale);

	gnm_expr_conventions_free (state.exprconv);
	g_object_unref (G_OBJECT (state.output));
	if (gzout)
		gsf_output_close (gzout);
}

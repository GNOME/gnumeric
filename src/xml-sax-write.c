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

#include <gsf/gsf-libxml.h>
#include <gsf/gsf-output-gzip.h>
#include <gsf/gsf-utils.h>
#include <locale.h>

typedef struct {
	IOContext 	*context;	/* The IOcontext managing things */
	WorkbookView const *wb_view;	/* View for the new workbook */
	Workbook const	   *wb;		/* The new workbook */

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
	gsf_output_xml_end_element (state->output);
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
	gsf_output_xml_end_element (state->output);
}

static void
xml_write_summary (GnmOutputXML *state)
{
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

	state.context = io_context;
	state.wb_view = wb_view;
	state.wb = wb_view_workbook (wb_view);
	state.output = gsf_output_xml_new (output);

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

	xml_write_attributes (&state);
	xml_write_summary    (&state);

	gsf_output_xml_end_element (state.output);

	gnumeric_setlocale (LC_MONETARY, old_monetary_locale);
	g_free (old_monetary_locale);
	gnumeric_setlocale (LC_NUMERIC, old_num_locale);
	g_free (old_num_locale);

	g_object_unref (G_OBJECT (state.output));
	if (gzout)
		gsf_output_close (gzout);
}

/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * stf-export.c : Structured Text Format Exporter (STF-E)
 *                Engine to construct CSV files
 *
 * Copyright (C) Almer. S. Tigelaar.
 * EMail: almer1@dds.nl or almer-t@bigfoot.com
 *
 * Based on the csv-io.c plugin by :
 *   Miguel de Icaza <miguel@gnu.org>
 *   Jody Goldberg   <jody@gnome.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gnumeric-config.h>
#include "gnumeric.h"
#include <glib/gi18n.h>
#include "stf-export.h"

#include "sheet.h"
#include "cell.h"
#include "value.h"
#include <gsf/gsf-output-iconv.h>
#include <gsf/gsf-impl-utils.h>

#include <stdio.h>
#include <string.h>


struct _GnmStfExport {
	GsfOutputCsv csv;

	GSList *sheet_list;
	char *charset;
	GnmStfTransliterateMode transliterate_mode;
	gboolean preserve_format;
};

static GObjectClass *parent_class;

typedef struct {
	GsfOutputCsvClass base_class;
} GnmStfExportClass;

enum {
	PROP_0,
	PROP_CHARSET,
	PROP_TRANSLITERATE_MODE,
	PROP_PRESERVE_FORMAT
};

/* ------------------------------------------------------------------------- */

/**
 * stf_export_options_sheet_list_clear:
 * @stfe: an export options struct
 *
 * Clears the sheet list.
 **/
void
stf_export_options_sheet_list_clear (GnmStfExport *stfe)
{
	g_return_if_fail (stfe != NULL);

	g_slist_foreach (stfe->sheet_list, (GFunc)g_object_unref, NULL);
	g_slist_free (stfe->sheet_list);
	stfe->sheet_list = NULL;
}


/**
 * stf_export_options_sheet_list_add:
 * @stfe: an export options struct
 * @sheet: a gnumeric sheet
 *
 * Appends a @sheet to the list of sheets to be exported
 **/
void
stf_export_options_sheet_list_add (GnmStfExport *stfe, Sheet *sheet)
{
	g_return_if_fail (stfe != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	g_object_ref (sheet);
	stfe->sheet_list = g_slist_append (stfe->sheet_list, sheet);
}

/* ------------------------------------------------------------------------- */

/**
 * stf_export_cell:
 * @stfe: an export options struct
 * @cell: the cell to write to the file
 *
 * Return value: return TRUE on success, FALSE otherwise.
 **/
static gboolean
stf_export_cell (GnmStfExport *stfe, GnmCell *cell)
{
	char *text = NULL;
	gboolean ok;
	g_return_val_if_fail (stfe != NULL, FALSE);

	if (cell) {
		if (stfe->preserve_format) 
			text = cell_get_rendered_text (cell);
		else if (cell->value)
			text = value_get_as_string (cell->value);
	}
		
	ok = gsf_output_csv_write_field (GSF_OUTPUT_CSV (stfe),
					 text ? text : "",
					 -1);
	g_free (text);

	return ok;
}

/**
 * stf_export_sheet:
 * @stfe: an export options struct
 * @sheet: the sheet to export
 *
 * Writes the @sheet to the callback function
 *
 * Return value: returns TRUE on success, FALSE otherwise
 **/
static gboolean
stf_export_sheet (GnmStfExport *stfe, Sheet *sheet)
{
	int col, row;
	GnmRange r;

	g_return_val_if_fail (stfe != NULL, FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);

	r = sheet_get_extent (sheet, FALSE);
	for (row = r.start.row; row <= r.end.row; row++) {
		for (col = r.start.col; col <= r.end.col; col++) {
			GnmCell *cell = sheet_cell_get (sheet, col, row);
			if (!stf_export_cell (stfe, cell))
				return FALSE;
		}
		if (!gsf_output_csv_write_eol (GSF_OUTPUT_CSV (stfe)))
			return FALSE;
	}

	return TRUE;
}

/**
 * stf_export:
 * @stfe: an export options struct
 *
 * Exports the sheets given in @stfe
 *
 * Return value: TRUE on success, FALSE otherwise
 **/
gboolean
stf_export (GnmStfExport *stfe)
{
	GSList *ptr;
	GsfOutput *sink;
	gboolean result = TRUE;

	g_return_val_if_fail (GNM_IS_STF_EXPORT (stfe), FALSE);
	g_return_val_if_fail (stfe->sheet_list != NULL, FALSE);
	g_object_get (G_OBJECT (stfe), "sink", &sink, NULL);
	g_return_val_if_fail (sink != NULL, FALSE);

	if (stfe->charset &&
	    strcmp (stfe->charset, "UTF-8") != 0) {
		char *charset;			
		GsfOutput *converter;

		switch (stfe->transliterate_mode) {
		default:
		case GNM_STF_TRANSLITERATE_MODE_ESCAPE:
			charset = g_strdup (stfe->charset);
			break;
		case GNM_STF_TRANSLITERATE_MODE_TRANS:
			charset = g_strconcat (stfe->charset,
					       "//TRANSLIT",
					       NULL);
			break;
		}
		converter = gsf_output_iconv_new (sink, charset, "UTF-8");
		g_free (charset);

		if (converter) {
			g_object_set (G_OBJECT (stfe), "sink", converter, NULL);
			g_object_unref (converter);
		} else {
			g_warning ("Failed to create converter.");
			result = FALSE;
		}
	}

	for (ptr = stfe->sheet_list; ptr != NULL ; ptr = ptr->next)
		if (!stf_export_sheet (stfe, ptr->data)) {
			result = FALSE;
			break;
		}

	g_object_set (G_OBJECT (stfe),
		      "sink", sink,
		      NULL);
	g_object_unref (sink);

	return result;
}

/* ------------------------------------------------------------------------- */

/**
 * stf_export_can_transliterate:
 *
 * Return value: TRUE iff //TRANSLIT is supported
 **/

gboolean
stf_export_can_transliterate (void)
{
	char const *text = "G\xc3\xbclzow";
	char *encoded_text;
	GError *error = NULL;

	encoded_text = g_convert (text, -1,
				  "ASCII//TRANSLIT", "UTF-8",
				  NULL, NULL, &error);
	g_free (encoded_text);

	if (error == NULL)
		return TRUE;

	g_error_free (error);
	return FALSE;
}

/* ------------------------------------------------------------------------- */

GType
gnm_stf_transliterate_mode_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
	  static const GEnumValue values[] = {
		  { GNM_STF_TRANSLITERATE_MODE_TRANS,  (char*)"GNM_STF_TRANSLITERATE_MODE_TRANS",  (char*)"transliterate" },
		  { GNM_STF_TRANSLITERATE_MODE_ESCAPE, (char*)"GNM_STF_TRANSLITERATE_MODE_ESCAPE", (char*)"escape" },
		  { 0, NULL, NULL }
	  };
	  etype = g_enum_register_static ("GnmStfTransliterateMode", values);
  }
  return etype;
}

/* ------------------------------------------------------------------------- */

static void
gnm_stf_export_init (GObject *obj)
{
}

/* ------------------------------------------------------------------------- */

static void
gnm_stf_export_finalize (GObject *obj)
{
	GnmStfExport *stfe = (GnmStfExport *)obj;

	stf_export_options_sheet_list_clear (stfe);
	g_free (stfe->charset);

	G_OBJECT_CLASS (parent_class)->finalize (obj);
}

/* ------------------------------------------------------------------------- */

static void
gnm_stf_export_get_property (GObject     *object,
			     guint        property_id,
			     GValue      *value,
			     GParamSpec  *pspec)
{
	GnmStfExport *stfe = (GnmStfExport *)object;

	switch (property_id) {
	case PROP_CHARSET:
		g_value_set_string (value, stfe->charset);
		break;
	case PROP_TRANSLITERATE_MODE:
		g_value_set_enum (value, stfe->transliterate_mode);
		break;
	case PROP_PRESERVE_FORMAT:
		g_value_set_boolean (value, stfe->preserve_format);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

/* ------------------------------------------------------------------------- */

static void
gnm_stf_export_set_property (GObject      *object,
			     guint         property_id,
			     GValue const *value,
			     GParamSpec   *pspec)
{
	GnmStfExport *stfe = (GnmStfExport *)object;
	char *scopy;

	switch (property_id) {
	case PROP_CHARSET:
		scopy = g_strdup (g_value_get_string (value));
		g_free (stfe->charset);
		stfe->charset = scopy;
		break;
	case PROP_TRANSLITERATE_MODE:
		stfe->transliterate_mode = g_value_get_enum (value);
		break;
	case PROP_PRESERVE_FORMAT:
		stfe->preserve_format = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

/* ------------------------------------------------------------------------- */

static void
gnm_stf_export_class_init (GObjectClass *gobject_class)
{
	parent_class = g_type_class_peek_parent (gobject_class);

	gobject_class->finalize = gnm_stf_export_finalize;
	gobject_class->get_property = gnm_stf_export_get_property;
	gobject_class->set_property = gnm_stf_export_set_property;

	g_object_class_install_property
		(gobject_class,
		 PROP_CHARSET,
		 g_param_spec_string ("charset",
				      _("Character set"),
				      _("The character encoding of the output."),
				      NULL,
				      GSF_PARAM_STATIC |
				      G_PARAM_READWRITE));
	g_object_class_install_property
		(gobject_class,
		 PROP_TRANSLITERATE_MODE,
		 g_param_spec_enum ("transliterate-mode",
				    _("Transliterate mode"),
				    _("What to do with unrepresentable characters."),
				    GNM_STF_TRANSLITERATE_MODE_TYPE,
				    GNM_STF_TRANSLITERATE_MODE_ESCAPE,
				    GSF_PARAM_STATIC |
				    G_PARAM_READWRITE));
	g_object_class_install_property
		(gobject_class,
		 PROP_PRESERVE_FORMAT,
		 g_param_spec_boolean ("preserve-format",
				       _("Preserve Format"),
				       _("Should cells' format be preserved"),
				       FALSE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));
}

/* ------------------------------------------------------------------------- */

GSF_CLASS (GnmStfExport, gnm_stf_export,
	   gnm_stf_export_class_init, gnm_stf_export_init, GSF_OUTPUT_CSV_TYPE)

/* ------------------------------------------------------------------------- */

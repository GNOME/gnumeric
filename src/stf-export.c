/*
 * stf-export.c : Structured Text Format Exporter (STF-E)
 *                Engine to construct CSV files
 *
 * Copyright (C) Almer. S. Tigelaar.
 * EMail: almer1@dds.nl or almer-t@bigfoot.com
 *
 * Based on the csv-io.c plugin by:
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <gnm-i18n.h>
#include <stf-export.h>

#include <gnumeric-conf.h>
#include <sheet.h>
#include <workbook.h>
#include <cell.h>
#include <value.h>
#include <gutils.h>
#include <gnm-format.h>
#include <gnm-datetime.h>
#include <gsf/gsf-output-iconv.h>
#include <gsf/gsf-output-memory.h>
#include <gsf/gsf-impl-utils.h>
#include <goffice/goffice.h>

#include <string.h>
#include <locale.h>

struct _GnmStfExport {
	GsfOutputCsv csv;

	GSList *sheet_list;
	char *charset;
	char *locale;
	GnmStfTransliterateMode transliterate_mode;
	GnmStfFormatMode format;
};

static GObjectClass *parent_class;

typedef struct {
	GsfOutputCsvClass base_class;
} GnmStfExportClass;

enum {
	PROP_0,
	PROP_CHARSET,
	PROP_LOCALE,
	PROP_TRANSLITERATE_MODE,
	PROP_FORMAT
};

/* ------------------------------------------------------------------------- */

static void
cb_sheet_destroyed (GnmStfExport *stfe, gpointer deadsheet)
{
	g_return_if_fail (GNM_IS_STF_EXPORT (stfe));

	stfe->sheet_list = g_slist_remove (stfe->sheet_list, deadsheet);
}

/**
 * gnm_stf_export_options_sheet_list_clear:
 * @stfe: an export options struct
 *
 * Clears the sheet list.
 **/
void
gnm_stf_export_options_sheet_list_clear (GnmStfExport *stfe)
{
	GSList *l;

	g_return_if_fail (GNM_IS_STF_EXPORT (stfe));


	for (l = stfe->sheet_list; l; l = l->next) {
		Sheet *sheet = l->data;

		g_object_weak_unref (G_OBJECT (sheet),
				     (GWeakNotify) cb_sheet_destroyed,
				     stfe);
	}

	g_slist_free (stfe->sheet_list);
	stfe->sheet_list = NULL;
}


/**
 * gnm_stf_export_options_sheet_list_add:
 * @stfe: an export options struct
 * @sheet: a gnumeric sheet
 *
 * Appends a @sheet to the list of sheets to be exported
 **/
void
gnm_stf_export_options_sheet_list_add (GnmStfExport *stfe, Sheet *sheet)
{
	g_return_if_fail (GNM_IS_STF_EXPORT (stfe));
	g_return_if_fail (IS_SHEET (sheet));

	g_object_weak_ref (G_OBJECT (sheet),
			   (GWeakNotify) cb_sheet_destroyed,
			   stfe);
	stfe->sheet_list = g_slist_append (stfe->sheet_list, sheet);
}


/**
 * gnm_stf_export_options_sheet_list_get:
 * @stfe: #GnmStfExport
 *
 * Returns: (element-type Sheet) (transfer none): the list of #Sheet instances
 * added to @stfe using gnm_stf_export_options_sheet_list_add().
 **/
GSList *
gnm_stf_export_options_sheet_list_get (const GnmStfExport *stfe)
{
	g_return_val_if_fail (GNM_IS_STF_EXPORT (stfe), NULL);

	return stfe->sheet_list;
}

/* ------------------------------------------------------------------------- */


static char *
try_auto_float (GnmValue *value, const GOFormat *format,
	       GODateConventions const *date_conv)
{
	gboolean is_date;
	int is_time;

	if (!VALUE_IS_FLOAT (value))
		return NULL;

	format = gnm_format_specialize (format, value);
	is_date = go_format_is_date (format) > 0;
	is_time = go_format_is_time (format);

	if (is_date || is_time > 0)
		return NULL;

	return format_value (go_format_general (), value, -1, date_conv);
}


static char *
try_auto_date (GnmValue *value, const GOFormat *format,
	       GODateConventions const *date_conv)
{
	gnm_float v, vr, vs;
	GOFormat *actual;
	char *res;
	gboolean needs_date, needs_time, needs_frac_sec;
	gboolean is_date;
	int is_time;
	GString *xlfmt;
	GDate date;

	format = gnm_format_specialize (format, value);
	is_date = go_format_is_date (format) > 0;
	is_time = go_format_is_time (format);

	if (!is_date && is_time <= 0)
		return NULL;

	/* We don't want to coerce strings.  */
	if (!VALUE_IS_FLOAT (value))
		return NULL;

	/* Verify that the date is valid.  */
	if (!datetime_value_to_g (&date, value, date_conv))
		return NULL;

	v = value_get_as_float (value);
	vr = gnm_fake_round (v);
	vs = (24 * 60 * 60) * gnm_abs (v - vr);

	needs_date = is_time < 2 && (is_date || gnm_abs (v) >= 1);
	needs_time = is_time > 0 || gnm_abs (v - vr) > GNM_const(1e-9);
	needs_frac_sec = needs_time &&
		gnm_abs (vs - gnm_fake_round (vs)) >= GNM_const(0.5e-3);

	xlfmt = g_string_new (NULL);
	if (needs_date) g_string_append (xlfmt, "yyyy/mm/dd");
	if (needs_time) {
		if (needs_date)
			g_string_append_c (xlfmt, ' ');
		if (is_time == 2)
			g_string_append (xlfmt, "[h]:mm:ss");
		else
			g_string_append (xlfmt, "hh:mm:ss");
		if (needs_frac_sec)
			g_string_append (xlfmt, ".000");
	}
	actual = go_format_new_from_XL (xlfmt->str);
	g_string_free (xlfmt, TRUE);
	res = format_value (actual, value, -1, date_conv);
	go_format_unref (actual);

	return res;
}

/**
 * stf_export_cell:
 * @stfe: an export options struct
 * @cell: the cell to write to the file
 *
 * Returns: %TRUE on success, %FALSE otherwise.
 **/
static gboolean
stf_export_cell (GnmStfExport *stfe, GnmCell *cell)
{
	char const *text = NULL;
	char *tmp = NULL;
	gboolean ok;
	g_return_val_if_fail (stfe != NULL, FALSE);

	if (cell) {
		switch (stfe->format) {
		case GNM_STF_FORMAT_PRESERVE:
			text = tmp = gnm_cell_get_rendered_text (cell);
			break;
		default:
		case GNM_STF_FORMAT_AUTO:
			if (cell->value) {
				GODateConventions const *date_conv =
					sheet_date_conv (cell->base.sheet);
				GOFormat const *format = gnm_cell_get_format (cell);
				text = tmp = try_auto_date (cell->value, format, date_conv);
				if (!text)
					text = tmp = try_auto_float (cell->value, format, date_conv);
				if (!text)
					text = value_peek_string (cell->value);
			}
			break;
		case GNM_STF_FORMAT_RAW:
			if (cell->value)
				text = value_peek_string (cell->value);
			break;
		}
	}

	ok = gsf_output_csv_write_field (GSF_OUTPUT_CSV (stfe),
					 text ? text : "",
					 -1);
	g_free (tmp);

	return ok;
}

/**
 * stf_export_sheet:
 * @stfe: an export options struct
 * @sheet: the sheet to export
 *
 * Writes the @sheet to the callback function
 *
 * Returns: %TRUE on success, %FALSE otherwise
 **/
static gboolean
stf_export_sheet (GnmStfExport *stfe, Sheet *sheet)
{
	int col, row;
	GnmRange r;
	int exporting;

	g_return_val_if_fail (stfe != NULL, FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);

	exporting = gnm_export_range_for_sheet (sheet, &r);
	if (exporting >= 0) {
		if (exporting == 0)
			return TRUE;
	} else
		r = sheet_get_extent (sheet, FALSE, TRUE);

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
 * gnm_stf_export:
 * @export_options: an export options struct
 *
 * Exports the sheets given in @stfe
 *
 * Returns: %TRUE on success, %FALSE otherwise
 **/
gboolean
gnm_stf_export (GnmStfExport *stfe)
{
	GSList *ptr;
	GsfOutput *sink;
	gboolean result = TRUE;
	char *old_locale = NULL;

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

	if (stfe->locale) {
		old_locale = g_strdup (go_setlocale (LC_ALL, NULL));
		go_setlocale (LC_ALL, stfe->locale);
	}

	for (ptr = stfe->sheet_list; ptr != NULL ; ptr = ptr->next) {
		Sheet *sheet = ptr->data;
		if (!stf_export_sheet (stfe, sheet)) {
			result = FALSE;
			break;
		}
	}

	if (old_locale)  /* go_setlocale clears the cache, */
		         /*so we don't want to call it with NULL*/
		go_setlocale (LC_ALL, old_locale);
	g_free (old_locale);


	g_object_set (G_OBJECT (stfe), "sink", sink, NULL);
	g_object_unref (sink);

	return result;
}

/* ------------------------------------------------------------------------- */

/**
 * gnm_stf_export_can_transliterate:
 *
 * Return value: %TRUE iff //TRANSLIT is supported
 **/
gboolean
gnm_stf_export_can_transliterate (void)
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
	  static GEnumValue const values[] = {
		  { GNM_STF_TRANSLITERATE_MODE_TRANS,  "GNM_STF_TRANSLITERATE_MODE_TRANS",  "transliterate" },
		  { GNM_STF_TRANSLITERATE_MODE_ESCAPE, "GNM_STF_TRANSLITERATE_MODE_ESCAPE", "escape" },
		  { 0, NULL, NULL }
	  };
	  etype = g_enum_register_static ("GnmStfTransliterateMode", values);
  }
  return etype;
}

/* ------------------------------------------------------------------------- */

GType
gnm_stf_format_mode_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
	  static GEnumValue const values[] = {
		  { GNM_STF_FORMAT_AUTO,     "GNM_STF_FORMAT_AUTO",     "automatic" },
		  { GNM_STF_FORMAT_RAW,      "GNM_STF_FORMAT_RAW",      "raw" },
		  { GNM_STF_FORMAT_PRESERVE, "GNM_STF_FORMAT_PRESERVE", "preserve" },
		  { 0, NULL, NULL }
	  };
	  etype = g_enum_register_static ("GnmStfFormatMode", values);
  }
  return etype;
}

/* ------------------------------------------------------------------------- */

static void
gnm_stf_export_init (G_GNUC_UNUSED GObject *obj)
{
}

/* ------------------------------------------------------------------------- */

static void
gnm_stf_export_finalize (GObject *obj)
{
	GnmStfExport *stfe = (GnmStfExport *)obj;

	gnm_stf_export_options_sheet_list_clear (stfe);
	g_free (stfe->charset);
	g_free (stfe->locale);

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
	case PROP_LOCALE:
		g_value_set_string (value, stfe->locale);
		break;
	case PROP_TRANSLITERATE_MODE:
		g_value_set_enum (value, stfe->transliterate_mode);
		break;
	case PROP_FORMAT:
		g_value_set_enum (value, stfe->format);
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
		scopy = g_value_dup_string (value);
		g_free (stfe->charset);
		stfe->charset = scopy;
		break;
	case PROP_LOCALE:
		scopy = g_value_dup_string (value);
		g_free (stfe->locale);
		stfe->locale = scopy;
		break;
	case PROP_TRANSLITERATE_MODE:
		stfe->transliterate_mode = g_value_get_enum (value);
		break;
	case PROP_FORMAT:
		stfe->format = g_value_get_enum (value);
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
				      P_("Character set"),
				      P_("The character encoding of the output."),
				      NULL,
				      GSF_PARAM_STATIC |
				      G_PARAM_READWRITE));
	g_object_class_install_property
		(gobject_class,
		 PROP_LOCALE,
		 g_param_spec_string ("locale",
				      P_("Locale"),
				      P_("The locale to use for number and date formatting."),
				      NULL,
				      GSF_PARAM_STATIC |
				      G_PARAM_READWRITE));
	g_object_class_install_property
		(gobject_class,
		 PROP_TRANSLITERATE_MODE,
		 g_param_spec_enum ("transliterate-mode",
				    P_("Transliterate mode"),
				    P_("What to do with unrepresentable characters."),
				    GNM_STF_TRANSLITERATE_MODE_TYPE,
				    GNM_STF_TRANSLITERATE_MODE_ESCAPE,
				    GSF_PARAM_STATIC |
				    G_PARAM_READWRITE));
	g_object_class_install_property
		(gobject_class,
		 PROP_FORMAT,
		 g_param_spec_enum ("format",
				    P_("Format"),
				    P_("How should cells be formatted?"),
				    GNM_STF_FORMAT_MODE_TYPE,
				    GNM_STF_FORMAT_AUTO,
				    GSF_PARAM_STATIC |
				    G_PARAM_READWRITE));
}

/* ------------------------------------------------------------------------- */

GSF_CLASS (GnmStfExport, gnm_stf_export,
	   gnm_stf_export_class_init, gnm_stf_export_init, GSF_OUTPUT_CSV_TYPE)

/* ------------------------------------------------------------------------- */
/* ------------------------------------------------------------------------- */


#include <wbc-gtk.h>
#include <dialogs/dialog-stf-export.h>
#include <workbook-view.h>

/**
 * gnm_stf_get_stfe:
 * @obj: #GObject with a #GnmStfExport attached as data.
 *
 * If none is found, a new one is created and attached to @obj.
 * Returns: (transfer none): the #GnmStfExport.
 **/
GnmStfExport *
gnm_stf_get_stfe (GObject *obj)
{
	GnmStfExport *stfe = g_object_get_data (obj, "stfe");
	if (!stfe) {
		const char * sep = gnm_conf_get_stf_export_separator ();
		const char * string_indicator = gnm_conf_get_stf_export_stringindicator ();
		const char * terminator = gnm_conf_get_stf_export_terminator ();
		const char * locale = gnm_conf_get_stf_export_locale ();
		const char * encoding = gnm_conf_get_stf_export_encoding ();
		int quotingmode = gnm_conf_get_stf_export_quoting ();
		int format = gnm_conf_get_stf_export_format ();
		int transliteratemode = gnm_conf_get_stf_export_transliteration () ?
			GNM_STF_TRANSLITERATE_MODE_TRANS : GNM_STF_TRANSLITERATE_MODE_ESCAPE;
		GString *triggers = g_string_new (NULL);

		if (*locale == 0)
			locale = NULL;
		if (*encoding == 0)
			encoding = NULL;

		/* Workaround GConf bug #641807. */
		if (terminator == NULL || *terminator == 0)
			terminator = "\n";

		if (quotingmode == GSF_OUTPUT_CSV_QUOTING_MODE_AUTO) {
			g_string_append (triggers, " \t");
			g_string_append (triggers, terminator);
			g_string_append (triggers, string_indicator);
			g_string_append (triggers, sep);
		}

		stfe = g_object_new (GNM_STF_EXPORT_TYPE,
				     "quoting-triggers", triggers->str,
				     "separator", sep,
				     "quote", string_indicator,
				     "eol", terminator,
				     "charset", encoding,
				     "locale", locale,
		      "quoting-mode", quotingmode,
		      "transliterate-mode", transliteratemode,
		      "format", format,
				     NULL);
		g_object_set_data_full (obj, "stfe", stfe, g_object_unref);
		g_string_free (triggers, TRUE);
	}
	return stfe;
}

static void
gnm_stf_file_saver_save (G_GNUC_UNUSED GOFileSaver const *fs,
			 GOIOContext *context,
			 GoView const *view, GsfOutput *output)
{
	WorkbookView *wbv = GNM_WORKBOOK_VIEW (view);
	Workbook *wb = wb_view_get_workbook (wbv);
	GnmStfExport *stfe = gnm_stf_get_stfe (G_OBJECT (wb));
	GsfOutput *dummy_sink;
	gboolean nosheets;

	/* TODO: move this GUI dependent code out of this
	 * filesaver into gui-file.c. After this, remove includes (see above). */
	if (GNM_IS_WBC_GTK (context->impl)) {
		gboolean cancelled =
			stf_export_dialog (WBC_GTK (context->impl), stfe, wb);
		if (cancelled) {
			go_io_error_unknown (context);
			return;
		}
	}

	nosheets = (stfe->sheet_list == NULL);
	if (nosheets) {
		GPtrArray *sel = gnm_file_saver_get_sheets (fs, wbv, TRUE);
		unsigned ui;
		for (ui = 0; ui < sel->len; ui++)
			gnm_stf_export_options_sheet_list_add
				(stfe, g_ptr_array_index (sel, ui));
		g_ptr_array_unref (sel);
	}

	g_object_set (G_OBJECT (stfe), "sink", output, NULL);
	if (gnm_stf_export (stfe) == FALSE)
		go_cmd_context_error_import (GO_CMD_CONTEXT (context),
			_("Error while trying to export file as text"));

	/* We're not allowed to set a NULL sink, so use a dummy.  */
	dummy_sink = gsf_output_memory_new ();
	g_object_set (G_OBJECT (stfe), "sink", dummy_sink, NULL);
	g_object_unref (dummy_sink);

	if (nosheets)
		gnm_stf_export_options_sheet_list_clear (stfe);
}

struct cb_set_export_option {
	GOFileSaver *fs;
	Workbook const *wb;
};

static gboolean
cb_set_export_option (const char *key, const char *value,
		      GError **err, gpointer user_)
{
	struct cb_set_export_option *user = user_;
	Workbook const *wb = user->wb;
	GnmStfExport *stfe = gnm_stf_get_stfe (G_OBJECT (wb));
	const char *errtxt;

	if (strcmp (key, "eol") == 0) {
		const char *eol;
		if (g_ascii_strcasecmp ("unix", value) == 0)
			eol = "\n";
		else if (g_ascii_strcasecmp ("mac", value) == 0)
			eol = "\r";
		else if	(g_ascii_strcasecmp ("windows", value) == 0)
			eol = "\r\n";
		else {
			errtxt = _("eol must be one of unix, mac, and windows");
			goto error;
		}

		g_object_set (G_OBJECT (stfe), "eol", eol, NULL);
		return FALSE;
	}

	if (strcmp (key, "charset") == 0 ||
	    strcmp (key, "locale") == 0 ||
	    strcmp (key, "quote") == 0 ||
	    strcmp (key, "separator") == 0 ||
	    strcmp (key, "format") == 0 ||
	    strcmp (key, "transliterate-mode") == 0 ||
	    strcmp (key, "quoting-mode") == 0 ||
	    strcmp (key, "quoting-on-whitespace") == 0)
		return go_object_set_property
			(G_OBJECT (stfe),
			 key, key, value,
			 err,
			 (_("Invalid value for option %s: \"%s\"")));

	return gnm_file_saver_common_export_option (user->fs, wb,
						    key, value, err);

error:
	if (err)
		*err = g_error_new (go_error_invalid (), 0, "%s", errtxt);

	return TRUE;
}

static gboolean
gnm_stf_fs_set_export_options (GOFileSaver *fs,
			       GODoc *doc,
			       const char *options,
			       GError **err,
			       G_GNUC_UNUSED gpointer user)
{
	GnmStfExport *stfe = gnm_stf_get_stfe (G_OBJECT (doc));
	struct cb_set_export_option data;
	data.fs = fs;
	data.wb = WORKBOOK (doc);
	gnm_stf_export_options_sheet_list_clear (stfe);
	return go_parse_key_value (options, err, cb_set_export_option, &data);
}

/**
 * gnm_stf_file_saver_create:
 * @id:
 *
 * Returns: (transfer full): the newly allocated #GOFileSaver.
 **/
GOFileSaver *
gnm_stf_file_saver_create (gchar const *id)
{
	GOFileSaver *fs = go_file_saver_new (id,
					     "txt",
					     _("Text (configurable)"),
					     GO_FILE_FL_WRITE_ONLY,
					     gnm_stf_file_saver_save);
	go_file_saver_set_save_scope (fs, GO_FILE_SAVE_WORKBOOK);
	g_object_set (G_OBJECT (fs), "sheet-selection", TRUE, NULL);
	g_signal_connect (G_OBJECT (fs), "set-export-options",
			  G_CALLBACK (gnm_stf_fs_set_export_options),
			  NULL);
	return GO_FILE_SAVER (fs);
}

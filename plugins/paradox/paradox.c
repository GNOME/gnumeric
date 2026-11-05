/**
 * paradox.c: Paradox support for Gnumeric
 *
 * Author:
 *    Uwe Steinmann <uwe@steinmann.cx>
 **/
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include "px.h"

#include <workbook-view.h>
#include <workbook.h>
#include <cell.h>
#include <value.h>
#include <gnm-plugin.h>
#include <sheet.h>
#include <ranges.h>
#include <mstyle.h>
#include <sheet-style.h>

#include <goffice/goffice.h>

#include <glib/gstdio.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Size of the sheets we create.  Used to validate the (untrusted) field and
 * record counts from the file header. */
#define PX_SHEET_COLS 256
#define PX_SHEET_ROWS 65536

GNM_PLUGIN_MODULE_HEADER;

static void gn_errorhandler (pxdoc_t *p, int error, const char *str, void *data) { g_warning ("%s", str); }
#ifndef PX_MEMORY_DEBUGGING
static void *gn_malloc	(pxdoc_t *p, size_t len, const char *caller)		{ return ((void *) g_malloc (len)); }
static void *gn_realloc (pxdoc_t *p, void *mem, size_t len, const char *caller)	{ return ((void *) g_realloc ((gpointer) mem, len)); }
static void  gn_free	(pxdoc_t *p, void *ptr)					{ g_free ((gpointer) ptr); }
#endif

static void
paradox_dispose (pxdoc_t *pxdoc)
{
	PX_close (pxdoc);
	PX_delete (pxdoc);
}

void paradox_file_open (GOFileOpener const *fo, GOIOContext *io_context,
                        WorkbookView *wb_view, GsfInput *input);
G_MODULE_EXPORT void
paradox_file_open (GOFileOpener const *fo, GOIOContext *io_context,
                   WorkbookView *wb_view, GsfInput *input)
{
	Workbook  *wb;
	pxdoc_t	  *pxdoc;
	pxhead_t	*pxh;
	pxfield_t	*pxf;
	char	*data;
	char	  *name;
	char const *tablename;
	Sheet	  *sheet;
	GnmCell	  *cell;
	GnmValue	  *val = NULL;
	guint row, i, j, offset;
	int numfields, extra_cols;
	gsize bufsize;
	/* Column type letters, indexed by the Paradox field type code */
	static const char ctypes[] = {'?',
				      'A', 'D', 'S', 'I', '$', 'N', '?', '?',
				      'L', '?', '?', 'M', 'B', 'F', 'O', 'G',
				      '?', '?', '?', 'T', '@', '+', '#', 'Y',
				      };

#ifdef PX_MEMORY_DEBUGGING
	PX_mp_init ();
#endif

#ifdef PX_MEMORY_DEBUGGING
	pxdoc = PX_new2 (gn_errorhandler, PX_mp_malloc, PX_mp_realloc, PX_mp_free);
#else
	pxdoc = PX_new2 (gn_errorhandler, gn_malloc, gn_realloc, gn_free);
#endif
	if (pxdoc == NULL) {
		go_io_error_string (io_context, _("Error while opening Paradox file."));
		return;
	}
	if (PX_open_gsf (pxdoc, input) < 0) {
		go_io_error_string (io_context, _("Error while opening Paradox file."));
		PX_delete (pxdoc);
		return;
	}
	pxh = pxdoc->px_head;
	if (pxh == NULL) {
		go_io_error_string (io_context, _("Error while opening Paradox file."));
		PX_delete (pxdoc);
		return;
	}

	/* The header is untrusted; make sure everything fits in the sheet */
	numfields = pxh->px_numfields;
	extra_cols = (pxh->px_filetype == pxfFileTypPrimIndex) ? 4 : 0;
	if (numfields < 0 || numfields > PX_SHEET_COLS - extra_cols) {
		go_io_error_string (io_context, _("The Paradox file has too many fields."));
		paradox_dispose (pxdoc);
		return;
	}

	/* Records are read into a buffer of the size given by the header, but
	 * fields are located using the field lengths.  Make sure the latter
	 * never reach beyond the end of the buffer. */
	bufsize = pxh->px_recordsize > 0 ? (gsize) pxh->px_recordsize : 0;
	{
		gsize needed = extra_cols ? 6 : 0;
		pxf = pxh->px_fields;
		for (i = 0; i < (guint) numfields; i++, pxf++) {
			if (pxf->px_flen < 0) {
				go_io_error_string (io_context, _("The Paradox file has an invalid field length."));
				paradox_dispose (pxdoc);
				return;
			}
			needed += pxf->px_flen;
		}
		if (needed > bufsize)
			bufsize = needed;
		if (bufsize == 0)
			bufsize = 1;
	}

	PX_set_targetencoding (pxdoc, "UTF-8");

	wb = wb_view_get_workbook (wb_view);
	/* The table name is in the file's encoding and may be missing */
	tablename = pxh->px_tablename;
	if (tablename == NULL || *tablename == '\0' ||
	    !g_utf8_validate (tablename, -1, NULL))
		tablename = _("Table");
	name = workbook_sheet_get_free_name (wb, tablename, FALSE, TRUE);
	sheet = sheet_new (wb, name, PX_SHEET_COLS, PX_SHEET_ROWS);
	g_free (name);
	workbook_sheet_attach (wb, sheet);

	pxf = pxh->px_fields;
	for (i = 0 ; i < (guint) numfields; i++) {
		char *str;
		guchar ftype = (guchar) pxf->px_ftype;
		char ctype = ftype < G_N_ELEMENTS (ctypes) ? ctypes[ftype] : '?';
		cell = sheet_cell_fetch (sheet, i, 0);
		str = g_strdup_printf ("%s,%c,%d",
				       pxf->px_fname ? pxf->px_fname : "", ctype,
				       pxf->px_ftype == pxfBCD ? pxf->px_fdc : pxf->px_flen);
#if PXLIB_MAJOR_VERSION == 0 && (PXLIB_MINOR_VERION < 3 || (PXLIB_MAJOR_VERSION == 3 && PXLIB_MICRO_VERSION == 0))
		{
			char *str2 = NULL;
			/* Convert the field names to utf-8. This is actually in pxlib
			 * responsibility, but hasn't been implemented yet. For the mean time
			 * we *misuse* PX_get_data_alpha()
			 */
			if (PX_get_data_alpha (pxdoc, str, strlen (str), &str2) > 0 && str2 != NULL) {
				gnm_cell_set_text (cell, str2);
			} else {
				char *valid = g_utf8_make_valid (str, -1);
				gnm_cell_set_text (cell, valid);
				g_free (valid);
			}
			if (str2)
				pxdoc->free (pxdoc, str2);
		}
#else
		gnm_cell_set_text (cell, str);
#endif
		g_free (str);
		pxf++;
	}
	{
		GnmRange r;
		GnmStyle *bold = gnm_style_new ();
		gnm_style_set_font_bold (bold, TRUE);
		sheet_style_apply_range	(sheet,
			range_init (&r, 0, 0, numfields - 1, 0), bold);
	}

	if ((data = (char *) pxdoc->malloc (pxdoc, bufsize, _("Could not allocate memory for record."))) == NULL) {
		go_io_error_string (io_context, _("Error while opening Paradox file."));
		paradox_dispose (pxdoc);
		return;
	}
	memset (data, 0, bufsize);
	row = 1;
	for (j = 0; pxh->px_numrecords > 0 && j < (guint)pxh->px_numrecords; j++) {
		pxdatablockinfo_t pxdbinfo;
		int isdeleted = 0;
		if (row >= PX_SHEET_ROWS) {
			go_io_warning (io_context, "%s",
				       _("The Paradox file has more records than fit in a sheet; the remaining records were not imported."));
			break;
		}
		if (NULL != PX_get_record2 (pxdoc, j, data, &isdeleted, &pxdbinfo)) {
			offset = 0;
			pxf = pxh->px_fields;
			for (i = 0; i < (guint) pxh->px_numfields ; i++) {
				cell = sheet_cell_fetch (sheet, i, row);
				val = NULL;
				switch (pxf->px_ftype) {
				case pxfAlpha: {
					char *value;
					if (0 < PX_get_data_alpha (pxdoc, &data[offset], pxf->px_flen, &value)) {
						val = value_new_string_nocopy (value);
/*							value_set_fmt (val, field->fmt); */
					}
					break;
				}
				case pxfShort: {
					short int value;
					if (0 < PX_get_data_short (pxdoc, &data[offset], pxf->px_flen, &value)) {
						val = value_new_int (value);
					}
					break;
				}
				case pxfAutoInc:
				case pxfLong: {
					long value;
					if (0 < PX_get_data_long (pxdoc, &data[offset], pxf->px_flen, &value)) {
						val = value_new_int (value);
					}
					break;
				}
				case pxfCurrency:
				case pxfNumber: {
					double value;
					if (0 < PX_get_data_double (pxdoc, &data[offset], pxf->px_flen, &value)) {
						val = value_new_float (value);
						if (pxf->px_ftype == pxfCurrency)
							value_set_fmt (val, go_format_default_money ());
					}
					break;
				}
				case pxfTimestamp: {
					double value;
					if (0 < PX_get_data_double (pxdoc, &data[offset], pxf->px_flen, &value)) {
						value = value / 86400000.0;
						/* 693594 = number of days up to 31.12.1899 */
						value -= 693594;
						val = value_new_float (value);
						value_set_fmt (val, go_format_default_date_time ());
					}
					break;
				}
				case  pxfLogical: {
					char value;
					if (0 < PX_get_data_byte (pxdoc, &data[offset], pxf->px_flen, &value)) {
						val = value_new_bool (value ? TRUE : FALSE);
					}
					break;
				}
				case pxfDate: {
					long value;
					int year, month, day;
					GDate *date;
					if (0 < PX_get_data_long (pxdoc, &data[offset], pxf->px_flen, &value)) {
						PX_SdnToGregorian (value+1721425, &year, &month, &day);
						/* Empty or corrupt dates must not reach GDate */
						if (year >= 1 && year <= 9999 &&
						    g_date_valid_dmy (day, month, year)) {
							date = g_date_new_dmy (day, month, year);
							val = value_new_int (go_date_g_to_serial (date, NULL));
							value_set_fmt (val, go_format_default_date ());
							g_date_free (date);
						}
					}
					break;
				}
				case pxfTime: {
					long value;
					if (0 < PX_get_data_long (pxdoc, &data[offset], pxf->px_flen, &value)) {
						val = value_new_float (value/86400000.0);
						value_set_fmt (val, go_format_default_time ());
					}
					break;
				}
				case pxfBCD: {
					char *value;
					if (0 < PX_get_data_bcd (pxdoc, &data[offset], pxf->px_fdc, &value)) {
						val = value_new_string_nocopy (value);
					}
					break;
				}
				case pxfMemoBLOb: {
					char *value;
					int size, mod_nr;
					if (0 < PX_get_data_blob (pxdoc, &data[offset], pxf->px_flen, &mod_nr, &size, &value) &&
					    value != NULL) {
						/* The blob is not NUL-terminated, and not necessarily UTF-8 */
						if (size > 0)
							val = value_new_string_nocopy (g_utf8_make_valid (value, size));
						pxdoc->free (pxdoc, value);
					}
					break;
				}
				default:
					val = value_new_string_nocopy (
						g_strdup_printf (_("Field type %d is not supported."), pxf->px_ftype));
				}
				if (val)
					gnm_cell_set_value (cell, val);
				offset += pxf->px_flen;
				pxf++;
			}
			if (pxh->px_filetype == pxfFileTypPrimIndex) {
				short int value;
				cell = sheet_cell_fetch (sheet, i++, row);
				if (0 < PX_get_data_short (pxdoc, &data[offset], 2, &value)) {
					val = value_new_int (value);
					gnm_cell_set_value (cell, val);
				}
				offset += 2;
				cell = sheet_cell_fetch (sheet, i++, row);
				if (0 < PX_get_data_short (pxdoc, &data[offset], 2, &value)) {
					val = value_new_int (value);
					gnm_cell_set_value (cell, val);
				}
				offset += 2;
				cell = sheet_cell_fetch (sheet, i++, row);
				if (0 < PX_get_data_short (pxdoc, &data[offset], 2, &value)) {
					val = value_new_int (value);
					gnm_cell_set_value (cell, val);
				}
				cell = sheet_cell_fetch (sheet, i++, row);
				val = value_new_int (pxdbinfo.number);
				gnm_cell_set_value (cell, val);
			}
		}
		row++;
	}
	pxdoc->free (pxdoc, data);

	PX_close (pxdoc);
	PX_delete (pxdoc);

	sheet_flag_recompute_spans (sheet);
}

/*****************************************************************************/

gboolean
paradox_file_probe (GOFileOpener const *fo, GsfInput *input,
                    GOFileProbeLevel pl);

G_MODULE_EXPORT gboolean
paradox_file_probe (GOFileOpener const *fo, GsfInput *input,
                    GOFileProbeLevel pl)
{
	pxdoc_t		*pxdoc;

	pxdoc = PX_new ();
	if (pxdoc == NULL)
		return FALSE;
	if (PX_open_gsf (pxdoc, input) < 0) {
		PX_delete (pxdoc);
		return FALSE;
	}

	PX_close (pxdoc);
	PX_delete (pxdoc);

#ifdef PX_MEMORY_DEBUGGING
	PX_mp_list_unfreed ();
#endif

	return TRUE;
}

/*****************************************************************************/

/* Parse a field specification of the form "Name,Type[,Size|,Prec]" taken
 * from the first row of the sheet into PXF.  Returns NULL on success, or a
 * newly allocated error message. */
static char *
paradox_parse_field_spec (pxdoc_t *pxdoc, char const *spec, int idx,
			  pxfield_t *pxf)
{
	char const *comma = strchr (spec, ',');
	char const *type;
	gboolean needsize = FALSE, needprecision = FALSE;
	size_t len;

	if (comma == NULL)
		return g_strdup (_("Field specification must be a comma separated value (Name,Type,Size,Prec)."));
	len = comma - spec;

	/* Get the field Type */
	type = comma + 1;
	if (*type == '\0')
		return g_strdup_printf (_("%d. field specification ended unexpectedly."), idx + 1);
	if (*type == ',')
		return g_strdup_printf (_("%d. field specification misses type."), idx + 1);

	switch ((int) *type) {
	case 'S':
		pxf->px_ftype = pxfShort;
		pxf->px_flen = 2;
		break;
	case 'I':
		pxf->px_ftype = pxfLong;
		pxf->px_flen = 4;
		break;
	case 'A':
	case 'C':
		pxf->px_ftype = pxfAlpha;
		needsize = TRUE;
		break;
	case 'N':
		pxf->px_ftype = pxfNumber;
		pxf->px_flen = 8;
		break;
	case '$':
		pxf->px_ftype = pxfCurrency;
		pxf->px_flen = 8;
		break;
	case 'L':
		pxf->px_ftype = pxfLogical;
		pxf->px_flen = 1;
		break;
	case 'D':
		pxf->px_ftype = pxfDate;
		pxf->px_flen = 4;
		break;
	case '+':
		pxf->px_ftype = pxfAutoInc;
		pxf->px_flen = 4;
		break;
	case '@':
		pxf->px_ftype = pxfTimestamp;
		pxf->px_flen = 8;
		break;
	case 'T':
		pxf->px_ftype = pxfTime;
		pxf->px_flen = 4;
		break;
	case '#':
		pxf->px_ftype = pxfBCD;
		pxf->px_flen = 17;
		needprecision = TRUE;
		break;
	case 'M':
		pxf->px_ftype = pxfMemoBLOb;
		needsize = TRUE;
		break;
	case 'B':
		pxf->px_ftype = pxfBLOb;
		needsize = TRUE;
		break;
	case 'F':
		pxf->px_ftype = pxfFmtMemoBLOb;
		needsize = TRUE;
		break;
	case 'Y':
		pxf->px_ftype = pxfBytes;
		needsize = TRUE;
		break;
	default:
		return g_strdup_printf (_("%d. field type '%c' is unknown."), idx + 1, *type);
	}

	if (needsize || needprecision) {
		char *endptr;
		char const *rest;
		long n;

		/* find end of type definition */
		rest = strchr (type, ',');
		if (rest == NULL || rest[1] == '\0')
			return g_strdup (_("Field specification misses the column size."));
		rest++;
		n = strtol (rest, &endptr, 10);
		if (endptr == rest)
			return g_strdup (_("Field specification misses the column size."));
		if (n < (needsize ? 1 : 0) || n > (needsize ? 255 : 32))
			return g_strdup_printf (_("The size of the %d. field is out of range."), idx + 1);
		if (needsize)
			pxf->px_flen = (int) n;
		else
			pxf->px_fdc = (int) n;
		if (*endptr != '\0') {
			/* There is also precision which we do not care about. */
			g_warning (_("The remainder '%s' of the specification for field %d is being disregarded."), endptr + 1, idx + 1);
		}
	}

	pxf->px_fname = pxdoc->malloc (pxdoc, len + 1, _("Allocate memory for column name."));
	if (pxf->px_fname == NULL)
		return g_strdup_printf (_("Could not allocate memory for %d. field name."), idx + 1);
	memcpy (pxf->px_fname, spec, len);
	pxf->px_fname[len] = '\0';

	return NULL;
}

void paradox_file_save (GOFileSaver const *fs, GOIOContext *io_context,
			WorkbookView const *wb_view, GsfOutput *output);
G_MODULE_EXPORT void
paradox_file_save (GOFileSaver const *fs, GOIOContext *io_context,
		   WorkbookView const *wb_view, GsfOutput *output)
{
	Sheet *sheet;
	GnmRange r;
	gint row, i, nfields, header_row, recordsize;

	pxdoc_t *pxdoc = NULL;
	pxfield_t *pxf = NULL;
	char *data = NULL;
	char *tmpfilename = NULL;
	char *err = NULL;
	gboolean pxf_owned = FALSE;	/* PX_delete frees PXF once the file exists */
	gboolean opened = FALSE;
	int fd;

	sheet = wb_view_cur_sheet (wb_view);
	if (sheet == NULL) {
		go_io_error_string (io_context, _("Cannot get default sheet."));
		return;
	}

	r = sheet_get_extent (sheet, FALSE, TRUE);
	nfields = r.end.col - r.start.col + 1;
	header_row = r.start.row;

#ifdef PX_MEMORY_DEBUGGING
	pxdoc = PX_new2 (gn_errorhandler, PX_mp_malloc, PX_mp_realloc, PX_mp_free);
#else
	pxdoc = PX_new2 (gn_errorhandler, gn_malloc, gn_realloc, gn_free);
#endif
	if (pxdoc == NULL) {
		go_io_error_string (io_context, _("Cannot allocate memory for field definitions."));
		return;
	}

	/* Read the field specification and build the field array for
	 * PX_create_fp(). Once the file has been created, the memory is freed
	 * by PX_delete() including the memory for the field names. */
	pxf = (pxfield_t *) pxdoc->malloc (pxdoc, nfields * sizeof (pxfield_t),
					   _("Allocate memory for field definitions."));
	if (pxf == NULL) {
		err = g_strdup (_("Cannot allocate memory for field definitions."));
		goto done;
	}
	memset (pxf, 0, nfields * sizeof (pxfield_t));

	for (i = 0; i < nfields; i++) {
		GnmCell *cell = sheet_cell_get (sheet, r.start.col + i, header_row);
		char *spec;

		if (gnm_cell_is_empty (cell)) {
			err = g_strdup (_("First line of sheet must contain database specification."));
			goto done;
		}
		spec = gnm_cell_get_rendered_text (cell);
		err = paradox_parse_field_spec (pxdoc, spec, i, &pxf[i]);
		g_free (spec);
		if (err)
			goto done;
	}

	/* Create the paradox file */
	fd = g_file_open_tmp ("pxtemp-XXXXXX", &tmpfilename, NULL);
	if (fd == -1) {
		err = g_strdup (_("Could not create output file."));
		goto done;
	}
	g_close (fd, NULL);

	if (0 > PX_create_file (pxdoc, pxf, nfields, tmpfilename, pxfFileTypNonIndexDB)) {
		err = g_strdup (_("Could not create output file."));
		goto done;
	}
	pxf_owned = TRUE;
	opened = TRUE;

	PX_set_inputencoding (pxdoc, "UTF-8");
	PX_set_parameter (pxdoc, "targetencoding", "CP1252");
	PX_set_tablename (pxdoc, sheet->name_unquoted);

	recordsize = pxdoc->px_head->px_recordsize;
	if ((data = (char *) pxdoc->malloc (pxdoc, recordsize, _("Allocate memory for record data."))) == NULL) {
		err = g_strdup (_("Could not allocate memory for record data."));
		goto done;
	}
	/* Process all cells */
	for (row = header_row + 1; row <= r.end.row; row++) {
		int offset = 0;
		memset (data, 0, recordsize);
		for (i = 0; i < nfields; i++) {
			GnmCell *cell = sheet_cell_get (sheet, r.start.col + i, row);
			if (!gnm_cell_is_empty (cell)) {
				char *fieldstr = gnm_cell_get_rendered_text (cell);
				switch (pxf[i].px_ftype) {
				case pxfShort: {
					int value = value_get_as_int (cell->value);
					PX_put_data_short (pxdoc, &data[offset], 2, (short int) value);
					break;
				}
				case pxfLong:
				case pxfAutoInc: {
					int value = value_get_as_int (cell->value);
					PX_put_data_long (pxdoc, &data[offset], 4, value);
					break;
				}
				case pxfTimestamp: {
					double value = value_get_as_float (cell->value);
					/* 60 would be 29.2.1900 which didn't exist. */
					if (value < 60)
						value += 1.0;
					value += 693594;
					value *= 86400000.0;
					PX_put_data_double (pxdoc, &data[offset], 8, value);
					break;
				}
				case pxfCurrency:
				case pxfNumber: {
					double value = value_get_as_float (cell->value);
					PX_put_data_double(pxdoc, &data[offset], 8, value);
					break;
				}
				case pxfAlpha: {
					char *value = fieldstr;
					int nlen = strlen (value);
					if (nlen > pxf[i].px_flen)
						/* xgettext : last %d gives the number of characters.*/
						/* This is input to ngettext. */
						g_warning
							(ngettext
							 ("Field %d in line %d has possibly "
							  "been cut off. Data has %d character.",
							  "Field %d in line %d has possibly "
							  "been cut off. Data has %d characters.",
							  nlen),
							 i+1, row+1, nlen);
					PX_put_data_alpha (pxdoc, &data[offset], pxf[i].px_flen, value);
					break;
				}
				case pxfMemoBLOb:
				case pxfFmtMemoBLOb: {
					char *value = fieldstr;
					if (0 > PX_put_data_blob (pxdoc, &data[offset], pxf[i].px_flen, value, strlen (value))) {
						g_warning (_("Field %d in row %d could not be written."), i+1, row+1);
					}
					break;
				}
				case pxfDate: {
					long value = value_get_as_int (cell->value);
					/* 60 would be 29.2.1900 which didn't exist. */
					if (value < 60)
						value++;
					value += 693594;
					PX_put_data_long (pxdoc, &data[offset], 4, value);
					break;
				}
				case pxfTime: {
					double dtmp;
					int value;
					dtmp  = value_get_as_float (cell->value);
					dtmp -= ((int) dtmp);
					value = (int) (dtmp * 86400000.0);
					PX_put_data_long (pxdoc, &data[offset], 4, value);
					break;
				}
				case pxfLogical: {
					gboolean berr; /* Ignored */
					gboolean value = value_get_as_bool (cell->value, &berr);
					PX_put_data_byte (pxdoc, &data[offset], 1, value ? 1 : 0);
					break;
				}
				case pxfBCD:
					PX_put_data_bcd (pxdoc, &data[offset], pxf[i].px_fdc, fieldstr);
					break;
				}
				g_free (fieldstr);
			}
			offset += pxf[i].px_flen;
		}
		if (0 > PX_put_record (pxdoc, data)) {
			err = g_strdup_printf (_("Could not write record number %d."), row - header_row);
			goto done;
		}
	}

	/* Close the file so that everything is flushed before reading it back */
	opened = FALSE;
	PX_close (pxdoc);

	{
		FILE *fp = g_fopen (tmpfilename, "rb");
		if (fp) {
			size_t size;
			char *buf = g_malloc (8192);
			while (0 != (size = fread (buf, 1, 8192, fp))) {
				if (!gsf_output_write (output, size, buf)) {
					err = g_strdup (_("Could not write output file."));
					break;
				}
			}
			fclose (fp);
			g_free (buf);
		} else
			err = g_strdup_printf (_("Cannot open %s"), tmpfilename);
	}

done:
	if (data)
		pxdoc->free (pxdoc, data);
	if (pxf && !pxf_owned) {
		for (i = 0; i < nfields; i++)
			if (pxf[i].px_fname)
				pxdoc->free (pxdoc, pxf[i].px_fname);
		pxdoc->free (pxdoc, pxf);
	}
	if (opened)
		PX_close (pxdoc);
	PX_delete (pxdoc);

#ifdef PX_MEMORY_DEBUGGING
	PX_mp_list_unfreed ();
#endif

	if (tmpfilename) {
		g_unlink (tmpfilename);
		g_free (tmpfilename);
	}
	if (err) {
		go_io_error_string (io_context, err);
		g_free (err);
	}
}

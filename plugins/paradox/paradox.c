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

#include <string.h>
#include <unistd.h>

GNM_PLUGIN_MODULE_HEADER;

#ifdef PX_MEMORY_DEBUGGING
static void gn_errorhandler (pxdoc_t *p, int error, const char *str, void *data) { g_warning ("%s", str); }
#else
static void *gn_malloc	(pxdoc_t *p, size_t len, const char *caller)		{ return ((void *) g_malloc (len)); }
static void *gn_realloc (pxdoc_t *p, void *mem, size_t len, const char *caller)	{ return ((void *) g_realloc ((gpointer) mem, len)); }
static void  gn_free	(pxdoc_t *p, void *ptr)					{ g_free ((gpointer) ptr); ptr = NULL; }
#endif

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
	Sheet	  *sheet;
	GnmCell	  *cell;
	GnmValue	  *val = NULL;
	GOErrorInfo *open_error = NULL;
	guint row, i, j, offset;

#ifdef PX_MEMORY_DEBUGGING
	PX_mp_init ();
#endif

#ifdef PX_MEMORY_DEBUGGING
	pxdoc = PX_new2 (gn_errorhandler, PX_mp_malloc, PX_mp_realloc, PX_mp_free);
#else
	pxdoc = PX_new2 (gn_errorhandler, gn_malloc, gn_realloc, gn_free);
#endif
	if (PX_open_gsf (pxdoc, input) < 0) {
		go_io_error_info_set (io_context, go_error_info_new_str_with_details (
					    _("Error while opening Paradox file."),
					    open_error));
		return;
	}
	pxh = pxdoc->px_head;

	PX_set_targetencoding (pxdoc, "UTF-8");

	wb = wb_view_get_workbook (wb_view);
	name = workbook_sheet_get_free_name (wb, pxh->px_tablename, FALSE, TRUE);
	sheet = sheet_new (wb, name, 256, 65536);
	g_free (name);
	workbook_sheet_attach (wb, sheet);

	pxf = pxh->px_fields;
	for (i = 0 ; i < (guint) pxh->px_numfields; i++) {
		char str[30], *str2;
		char ctypes[26] = {'?',
				   'A', 'D', 'S', 'I', '$', 'N', '?', '?',
				   'L', '?', '?', 'M', 'B', 'F', 'O', 'G',
				   '?', '?', '?', 'T', '@', '+', '#', 'Y',
				   };
		cell = sheet_cell_fetch (sheet, i, 0);
		if (pxf->px_ftype == pxfBCD)
			snprintf (str, 30, "%s,%c,%d", pxf->px_fname, ctypes[(int)pxf->px_ftype], pxf->px_fdc);
		else
			snprintf (str, 30, "%s,%c,%d", pxf->px_fname, ctypes[(int)pxf->px_ftype], pxf->px_flen);
#if PXLIB_MAJOR_VERSION == 0 && (PXLIB_MINOR_VERION < 3 || (PXLIB_MAJOR_VERSION == 3 && PXLIB_MICRO_VERSION == 0))
		/* Convert the field names to utf-8. This is actually in pxlib
		 * responsibility, but hasn't been implemented yet. For the mean time
		 * we *misuse* PX_get_data_alpha()
		 */
		PX_get_data_alpha (pxdoc, str, strlen (str), &str2);
		gnm_cell_set_text (cell, str2);
		pxdoc->free (pxdoc, str2);
#else
		gnm_cell_set_text (cell, str);
#endif
		pxf++;
	}
	{
		GnmRange r;
		GnmStyle *bold = gnm_style_new ();
		gnm_style_set_font_bold (bold, TRUE);
		sheet_style_apply_range	(sheet,
			range_init (&r, 0, 0, pxh->px_numfields-1, 0), bold);
	}

	if ((data = (char *) pxdoc->malloc (pxdoc, pxh->px_recordsize, _("Could not allocate memory for record."))) == NULL) {
		go_io_error_info_set (io_context, go_error_info_new_str_with_details (
					    _("Error while opening Paradox file."),
					    open_error));
		return;
	}
	row = 1;
	for (j = 0; j < (guint)pxh->px_numrecords; j++) {
		pxdatablockinfo_t pxdbinfo;
		int isdeleted = 0;
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
						date = g_date_new_dmy (day, month, year);
						val = value_new_int (go_date_g_to_serial (date, NULL));
						value_set_fmt (val, go_format_default_date ());
						g_date_free (date);
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
					if (0 < PX_get_data_blob (pxdoc, &data[offset], pxf->px_flen, &mod_nr, &size, &value)) {
						val = value_new_string_nocopy (value);
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
	pxhead_t	*pxh;

	pxdoc = PX_new ();
	if (PX_open_gsf (pxdoc, input) < 0) {
		return FALSE;
	}

	pxh = pxdoc->px_head;

	PX_close (pxdoc);
	PX_delete (pxdoc);

#ifdef PX_MEMORY_DEBUGGING
	PX_mp_list_unfreed ();
#endif

	return TRUE;
}

/*****************************************************************************/

void paradox_file_save (GOFileSaver const *fs, GOIOContext *io_context,
			WorkbookView const *wb_view, GsfOutput *output);
G_MODULE_EXPORT void
paradox_file_save (GOFileSaver const *fs, GOIOContext *io_context,
		   WorkbookView const *wb_view, GsfOutput *output)
{
	Sheet *sheet;
	GnmRange r;
	gint row, col, i;

	pxdoc_t *pxdoc = NULL;
	pxfield_t *pxf;
	char *data;
	char *tmpfilename;

	sheet = wb_view_cur_sheet (wb_view);
	if (sheet == NULL) {
		go_io_error_string (io_context, _("Cannot get default sheet."));
		return;
	}

	r = sheet_get_extent (sheet, FALSE, TRUE);

#ifdef PX_MEMORY_DEBUGGING
	pxdoc = PX_new2 (gn_errorhandler, PX_mp_malloc, PX_mp_realloc, PX_mp_free);
#else
	pxdoc = PX_new2 (gn_errorhandler, gn_malloc, gn_realloc, gn_free);
#endif

	/* Read the field specification and build the field array for
	 * PX_create_fp(). The memory is freed by PX_delete() including
	 * the memory for the field name. */
	if ((pxf = (pxfield_t *) pxdoc->malloc (pxdoc, (r.end.col+1)*sizeof (pxfield_t), _("Allocate memory for field definitions."))) == NULL){
		go_io_error_string (io_context, _("Cannot allocate memory for field definitions."));
		PX_delete (pxdoc);
		return;
	}

	for (col = r.start.col; col <= r.end.col; col++) {
		GnmCell *cell = sheet_cell_get (sheet, col, 0);
		if (gnm_cell_is_empty (cell)) {
			go_io_error_string (io_context, _("First line of sheet must contain database specification."));
			PX_delete (pxdoc);
			return;
		} else {
			gchar *fieldstr, *tmp;
			int len, needsize, needprecision;

			i = col;
			fieldstr = gnm_cell_get_rendered_text (cell);
			needsize = 0;
			needprecision = 0;

			/* Search for the first comma which is the end of the field name. */
			tmp = strchr (fieldstr, ',');
			if (NULL == tmp) {
				g_warning (_("Field specification must be a comma separated value (Name,Type,Size,Prec)."));
				PX_delete (pxdoc);
				return;
			}
			len = tmp-fieldstr;
			if (NULL == (pxf[i].px_fname = pxdoc->malloc (pxdoc, len+1, _("Allocate memory for column name.")))) {
				g_warning (_("Could not allocate memory for %d. field name."), i);
				PX_delete (pxdoc);
				return;
			}
			strncpy (pxf[i].px_fname, fieldstr, len);
			pxf[i].px_fname[len] = '\0';

			/* Get the field Type */
			fieldstr = tmp+1;
			if (*fieldstr == '\0') {
				g_warning (_("%d. field specification ended unexpectedly."), i);
				PX_delete (pxdoc);
				return;
			}
			if (*fieldstr == ',') {
				g_warning (_("%d. field specification misses type."), i);
				PX_delete (pxdoc);
				return;
			}
			switch ((int) *fieldstr) {
			case 'S':
				pxf[i].px_ftype = pxfShort;
				pxf[i].px_flen = 2;
				break;
			case 'I':
				pxf[i].px_ftype = pxfLong;
				pxf[i].px_flen = 4;
				break;
			case 'A':
			case 'C':
				pxf[i].px_ftype = pxfAlpha;
				needsize = 1;
				break;
			case 'N':
				pxf[i].px_ftype = pxfNumber;
				pxf[i].px_flen = 8;
				break;
			case '$':
				pxf[i].px_ftype = pxfCurrency;
				pxf[i].px_flen = 8;
				break;
			case 'L':
				pxf[i].px_ftype = pxfLogical;
				pxf[i].px_flen = 1;
				break;
			case 'D':
				pxf[i].px_ftype = pxfDate;
				pxf[i].px_flen = 4;
				break;
			case '+':
				pxf[i].px_ftype = pxfAutoInc;
				pxf[i].px_flen = 4;
				break;
			case '@':
				pxf[i].px_ftype = pxfTimestamp;
				pxf[i].px_flen = 8;
				break;
			case 'T':
				pxf[i].px_ftype = pxfTime;
				pxf[i].px_flen = 4;
				break;
			case '#':
				pxf[i].px_ftype = pxfBCD;
				pxf[i].px_flen = 17;
				needprecision = 1;
				break;
			case 'M':
				pxf[i].px_ftype = pxfMemoBLOb;
				needsize = 1;
				break;
			case 'B':
				pxf[i].px_ftype = pxfBLOb;
				needsize = 1;
				break;
			case 'F':
				pxf[i].px_ftype = pxfFmtMemoBLOb;
				needsize = 1;
				break;
			case 'Y':
				pxf[i].px_ftype = pxfBytes;
				needsize = 1;
				break;
			default:
				g_warning (_("%d. field type '%c' is unknown."), i, *fieldstr);
				pxdoc->free (pxdoc, pxf);
				PX_delete (pxdoc);
				return;
			}

			if (needsize || needprecision) {
				char *endptr;
				/* find end of type definition */
				tmp = strchr (fieldstr, ',');
				if (NULL == tmp || *(tmp+1) == '\0') {
					g_warning (_("Field specification misses the column size."));
					PX_delete (pxdoc);
					return;
				}
				fieldstr = tmp+1;
				if (needsize)
					pxf[i].px_flen = strtol (fieldstr, &endptr, 10);
				else
					pxf[i].px_fdc = strtol (fieldstr, &endptr, 10);
				if ((endptr == NULL) || (fieldstr == endptr)) {
					g_warning (_("Field specification misses the column size."));
					PX_delete (pxdoc);
					return;
				}
				if (*endptr != '\0') {
					/* There is also precision which we do not care about. */
					fieldstr = endptr+1;
					g_warning (_("The remainder '%s' of the specification for field %d is being disregarded."), fieldstr, i+1);
				}
			}
		}
	}

	/* Create the paradox file */
	tmpfilename = tempnam ("/tmp", NULL);
	if (0 > PX_create_file (pxdoc, pxf, r.end.col+1, tmpfilename, pxfFileTypNonIndexDB)) {
		g_warning (_("Could not create output file."));
		PX_delete (pxdoc);
		return;
	}

	PX_set_inputencoding (pxdoc, "UTF-8");
	PX_set_parameter (pxdoc, "targetencoding", "CP1252");
	PX_set_tablename (pxdoc, sheet->name_unquoted);

	if ((data = (char *) pxdoc->malloc (pxdoc, pxdoc->px_head->px_recordsize, _("Allocate memory for record data."))) == NULL) {
		g_warning (_("Could not allocate memory for record data."));
		PX_close (pxdoc);
		PX_delete (pxdoc);
		return;
	}
	/* Process all cells */
	for (row = r.start.row+1; row <= r.end.row; row++) {
		int i;
		int offset;
		offset = 0;
		memset (data, 0, pxdoc->px_head->px_recordsize);
		for (col = r.start.col, i = 0; col <= r.end.col; col++) {
			GnmCell *cell = sheet_cell_get (sheet, col, row);
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
					gboolean err; /* Ignored */
					gboolean value = value_get_as_bool (cell->value, &err);
					PX_put_data_byte (pxdoc, &data[offset], 1, value ? 1 : 0);
					break;
				}
				case pxfBCD:
					PX_put_data_bcd (pxdoc, &data[offset], pxf[i].px_fdc, fieldstr);
					break;
				}
			}
			offset += pxf[i].px_flen;
			i++;
		}
		if ((i > 0) && (0 > PX_put_record (pxdoc, data))) {
			g_warning (_("Could not write record number %d."), i+1);
			pxdoc->free (pxdoc, data);
			PX_close (pxdoc);
			PX_delete (pxdoc);
			return;
		}
	}
	pxdoc->free (pxdoc, data);
	PX_close (pxdoc);
	PX_delete (pxdoc);

#ifdef PX_MEMORY_DEBUGGING
	PX_mp_list_unfreed ();
#endif

	{
		FILE *fp;
		size_t size;
		fp = fopen (tmpfilename, "r");
		if (fp) {
			data = g_malloc (8192);
			while (0 != (size = fread (data, 1, 8192, fp)))
				gsf_output_write (output, size, data);
			fclose (fp);
			g_free (data);
		} else
			g_warning ("Cannot open %s\n", tmpfilename);

		unlink (tmpfilename);
	}
}


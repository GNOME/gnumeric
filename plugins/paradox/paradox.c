/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/**
 * boot.c: Paradox support for Gnumeric
 *
 * Author:
 *    Uwe Steinmann <uwe@steinmann.cx>
 **/
#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include <gnumeric.h>
#include <string.h>
#include "px.h"

#include <workbook-view.h>
#include <workbook.h>
#include <cell.h>
#include <value.h>
#include <plugin.h>
#include <plugin-util.h>
#include <module-plugin-defs.h>
#include <sheet.h>
#include <datetime.h>
#include <ranges.h>
#include <mstyle.h>
#include <sheet-style.h>
#include <io-context.h>

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

void paradox_file_open (GnmFileOpener const *fo, IOContext *io_context,
                        WorkbookView *wb_view, GsfInput *input);

gboolean
paradox_file_probe (GnmFileOpener const *fo, GsfInput *input,
                    FileProbeLevel pl);

static void *gn_malloc(pxdoc_t *p, size_t len, const char *caller) {
	return((void *) g_malloc(len));
}

static void *gn_realloc(pxdoc_t *p, void *mem, size_t len, const char *caller) {
	return((void *) g_realloc((gpointer) mem, len));
}

static void gn_free(pxdoc_t *p, void *ptr) {
	g_free((gpointer) ptr);
	ptr = NULL;
}

static void gn_errorhandler(pxdoc_t *p, int error, const char *str, void *data) {
				g_warning(str);
}

void
paradox_file_open (GnmFileOpener const *fo, IOContext *io_context,
                   WorkbookView *wb_view, GsfInput *input)
{
	Workbook  *wb;
	pxdoc_t	  *pxdoc;
	pxhead_t	*pxh;
	pxfield_t	*pxf;
	char	*data;
	char	  *name;
	Sheet	  *sheet = NULL;
	GnmCell	  *cell;
	GnmValue	  *val = NULL;
	ErrorInfo *open_error = NULL;
	guint row, i, j, offset;

	pxdoc = PX_new2(gn_errorhandler, gn_malloc, gn_realloc, gn_free);
	if (PX_open_gsf (pxdoc, input) < 0) {
		gnumeric_io_error_info_set (io_context, error_info_new_str_with_details (
		                            _("Error while opening paradox file."),
		                            open_error));
		return;
	}
	pxh = pxdoc->px_head;

	fprintf(stderr, "Paradox %2.1f file, %d records, %d columns\n", pxh->px_fileversion/10.0, pxh->px_numrecords, pxh->px_numfields);

	PX_set_targetencoding(pxdoc, "UTF-8");

	wb = wb_view_workbook (wb_view);
	name = workbook_sheet_get_free_name (wb, pxh->px_tablename, FALSE, TRUE);
	sheet = sheet_new (wb, name);
	g_free (name);
	workbook_sheet_attach (wb, sheet, NULL);

	pxf = pxh->px_fields;
	for (i = 0 ; i < (guint) pxh->px_numfields; i++) {
		char str[30], *str2;
		char ctypes[26] = {'?',
		                   'A', 'D', 'S', 'I', '$', 'N', '?', '?',
		                   'L', '?', '?', 'M', 'B', 'F', 'O', 'G',
		                   '?', '?', '?', 'T', '@', '+', '#', 'Y',
		                   };
		cell = sheet_cell_fetch (sheet, i, 0);
		snprintf (str, 30, "%s,%c,%d", pxf->px_fname, ctypes[(int)pxf->px_ftype], pxf->px_flen);
#if PXLIB_MAJOR_VERSION == 0 && (PXLIB_MINOR_VERION < 3 || (PXLIB_MAJOR_VERSION == 3 && PXLIB_MICRO_VERSION == 0))
		/* Convert the field names to utf-8. This is actually in pxlib
		 * responsibility, but hasn't been implemented. For the mean time
		 * we *misuse* PX_get_data_alpha()
		 */
		PX_get_data_alpha(pxdoc, str, strlen(str), &str2);
		fprintf(stderr, "%s\n", str2);
		cell_set_text (cell, str2);
#else
		cell_set_text (cell, str);
#endif
		pxf++;
	}
	{
		GnmRange r;
		GnmStyle *bold = mstyle_new ();
		mstyle_set_font_bold (bold, TRUE);
		sheet_style_apply_range	(sheet,
			range_init (&r, 0, 0, pxh->px_numfields-1, 0), bold);
	}

	if((data = (char *) pxdoc->malloc(pxdoc, pxh->px_recordsize, _("Could not allocate memory for record."))) == NULL) {
		gnumeric_io_error_info_set (io_context, error_info_new_str_with_details (
		                            _("Error while opening paradox file."),
		                            open_error));
		return;
	}
	row = 1;
	for(j = 0; j < (guint)pxh->px_numrecords; j++) {
		pxdatablockinfo_t pxdbinfo;
		int isdeleted = 0;
		if(NULL != PX_get_record2(pxdoc, j, data, &isdeleted, &pxdbinfo)) {
			offset = 0;
			pxf = pxh->px_fields;
			for (i = 0; i < (guint) pxh->px_numfields ; i++) {
				cell = sheet_cell_fetch (sheet, i, row);
				val = NULL;
				switch(pxf->px_ftype) {
					case pxfAlpha: {
						char *value;
						if(0 < PX_get_data_alpha(pxdoc, &data[offset], pxf->px_flen, &value)) {
							val = value_new_string_nocopy (value);
//							value_set_fmt (val, field->fmt);
						}
						break;
					}
					case pxfShort: {
						short int value;
						if(0 < PX_get_data_short(pxdoc, &data[offset], pxf->px_flen, &value)) {
							val = value_new_int (value);
						}
						break;
					}
					case pxfAutoInc:
					case pxfTimestamp:
					case pxfLong: {
						long value;
						if(0 < PX_get_data_long(pxdoc, &data[offset], pxf->px_flen, &value)) {
							val = value_new_int (value);
						}
						break;
					}
					case pxfCurrency:
					case pxfNumber: {
						double value;
						if(0 < PX_get_data_double(pxdoc, &data[offset], pxf->px_flen, &value)) {
							val = value_new_float (value);
						}
						break;
					}
					case  pxfLogical: {
						char value;
						if(0 < PX_get_data_byte(pxdoc, &data[offset], pxf->px_flen, &value)) {
							val = value_new_bool (value ? TRUE : FALSE);
						}
						break;
					}
					case pxfDate: {
						long value;
						int year, month, day;
						if(0 < PX_get_data_long(pxdoc, &data[offset], pxf->px_flen, &value)) {
							PX_SdnToGregorian(value+1721425, &year, &month, &day);
							GDate *date = g_date_new_dmy (day, month, year);
							val = value_new_int (datetime_g_to_serial (date, NULL));
							g_date_free (date);
						}
						break;
					}
					case pxfBCD: {
						char *value;
						if(0 < PX_get_data_bcd(pxdoc, &data[offset], pxf->px_fdc, &value)) {
							val = value_new_string_nocopy (value);
						}
						break;
					}
					default: {
						val = value_new_string_nocopy (
							g_strdup_printf (_("Field type %d is not supported."), pxf->px_ftype));
					}
				}
				if(val)
					cell_set_value (cell, val);
				offset += pxf->px_flen;
				pxf++;
			}
			if(pxh->px_filetype == pxfFileTypPrimIndex) {
				short int value;
				cell = sheet_cell_fetch (sheet, i++, row);
				if(0 < PX_get_data_short(pxdoc, &data[offset], 2, &value)) {
					val = value_new_int (value);
					cell_set_value (cell, val);
				}
				offset += 2;
				cell = sheet_cell_fetch (sheet, i++, row);
				if(0 < PX_get_data_short(pxdoc, &data[offset], 2, &value)) {
					val = value_new_int (value);
					cell_set_value (cell, val);
				}
				offset += 2;
				cell = sheet_cell_fetch (sheet, i++, row);
				if(0 < PX_get_data_short(pxdoc, &data[offset], 2, &value)) {
					val = value_new_int (value);
					cell_set_value (cell, val);
				}
				cell = sheet_cell_fetch (sheet, i++, row);
				val = value_new_int (pxdbinfo.number);
				cell_set_value (cell, val);
			}
		}
		row++;
	}
	pxdoc->free(pxdoc, data);

	PX_close (pxdoc);
	PX_delete (pxdoc);

	sheet_flag_recompute_spans (sheet);
}

gboolean
paradox_file_probe (GnmFileOpener const *fo, GsfInput *input,
                    FileProbeLevel pl)
{
	pxdoc_t	  *pxdoc;
	pxhead_t	*pxh;

	pxdoc = PX_new();
	if (PX_open_gsf (pxdoc, input) < 0) {
		return false;
	}

	pxh = pxdoc->px_head;

	PX_close (pxdoc);
	PX_delete (pxdoc);

	return true;
}


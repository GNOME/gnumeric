/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <libgnumeric.h>
#include "xbase.h"

#include <gnm-format.h>
#include <gutils.h>
#include <sheet.h>
#include <goffice/app/io-context.h>
#include <goffice/app/error-info.h>

#include <string.h>
#include <stdio.h>
#include <gsf/gsf-input.h>
#include <gsf/gsf-utils.h>
#include <gsf/gsf-msole-utils.h>

#define XBASE_DEBUG 0
#if XBASE_DEBUG > 0
#define d(level, code)	do { if (XBASE_DEBUG > level) { code } } while (0)
#else
#define d(level, code)
#endif

static char const * const field_types = "CNLDMF?BGPYTI";

#if XBASE_DEBUG > 0
static char const * const field_type_descriptions [] = {
	"Character", "Number", "Logical", "Date", "Memo", "Floating point",
	"Character name variable", "Binary", "General", "Picture", "Currency",
	"DateTime", "Integer"
};
#endif


/**
 * Newly allocated pointer to record, initialised as first in database.
*/
XBrecord *
record_new (XBfile *file)
{
	XBrecord *ans = g_new (XBrecord, 1);
	ans->file = file;
	ans->row = 1;
	/* ans->data = g_new (guint8, file->fieldlen); */
	ans->data = (guint8 *) g_strnfill (file->fieldlen, '?'); /* FIXME : just for testing */
	record_seek (ans, SEEK_SET, 1);
	return ans;
}

/**
 * Position record at requested row, and load raw data.  Returns FALSE on
 * invalid row, file error, or invalid whence (same values as in fseek).
 */
gboolean
record_seek (XBrecord *record, int whence, gsf_off_t row)
{
	gsf_off_t offset;
	switch (whence) {
	case SEEK_SET:
		offset = row;
		break;
	case SEEK_CUR:
		offset = record->row + row;
		break;
	case SEEK_END:
		offset = record->file->records + 1 - row;
		break;
	default:
		g_warning("record_seek: invalid whence (%d)", whence);
		return FALSE;
	}
	if (offset < 1 || offset > (int)record->file->records)
		return FALSE;
	record->row = offset;
	offset = (offset-1) * record->file->fieldlen + record->file->headerlen + 1;
	return !gsf_input_seek (record->file->input, offset, G_SEEK_SET) &&
	    gsf_input_read (record->file->input, record->file->fieldlen, record->data) != NULL;
}

/**
 * Clear allocated space for record.
 */
void
record_free (XBrecord *record)
{
	g_free (record->data);
	g_free (record);
}

/**
 * Points to binary data for num'th field in record's data.
 */
gchar *
record_get_field (XBrecord const *record, guint num)
{
	if (num >= record->file->fields)
		return NULL;
	return (gchar *)record->data + record->file->format [num]->pos;
}

static void
xbase_read_header (XBfile *x, ErrorInfo **ret_error)
{
	static struct {
		guint8 const id;
		int    const codepage;
		char const *const name;
	} const codepages [] = {
		{ 0x01, 437, "U.S. MS-DOS" },
		{ 0x69, 620, "Mazovia (Polish) MS-DOS" },
		{ 0x6A, 737, "Greek MS-DOS (437G)" },
		{ 0x02, 850, "International MS-DOS" },
		{ 0x64, 852, "Eastern European MS-DOS" },
		{ 0x6B, 857, "Turkish MS-DOS" },
		{ 0x67, 861, "Icelandic MS-DOS" },
		{ 0x66, 865, "Nordic MS-DOS" },
		{ 0x65, 866, "Russian MS-DOS" },
		{ 0x7C, 874, "Thai Windows" },
		{ 0x68, 895, "Kamenicky (Czech) MS-DOS" },
		{ 0x7B, 932, "Japanese Windows" },
		{ 0x7A, 936, "Chinese (PRC, Singapore) Windows" },
		{ 0x79, 949, "Korean Windows" },
		{ 0x78, 950, "Chinese (Hong Kong SAR, Taiwan) Windows" },
		{ 0xC8, 1250, "Eastern European Windows" },
		{ 0xC9, 1251, "Russian Windows" },
		{ 0x03, 1252, "Windows ANSI" },
		{ 0xCB, 1253, "Greek Windows" },
		{ 0xCA, 1254, "Turkish Windows" },
		{ 0x7D, 1255, "Hebrew Windows" },
		{ 0x7E, 1256, "Arabic Windows" },
		{ 0x04, 10000, "Standard Macintosh" },
		{ 0x98, 10006, "Greek Macintosh" },
		{ 0x96, 10007, "Russian Macintosh" },
		{ 0x97, 10029, "Macintosh EE" },
		{ 0x00, 0, NULL }
	};
	int i;
	guint8 hdr[32];

	if (gsf_input_read (x->input, 32, hdr) == NULL) {
		*ret_error = error_info_new_str (_("Failed to read DBF header."));
		return;
	}
	switch (hdr[0]) { /* FIXME: assuming dBASE III+, not IV */
	case 0x02: fprintf (stderr, "FoxBASE\n"); break;
	case 0x03: fprintf (stderr, "FoxBASE+/dBASE III PLUS, no memo\n"); break;
	case 0x30: fprintf (stderr, "Visual FoxPro\n"); break;
	case 0x43: fprintf (stderr, "dBASE IV SQL table files, no memo\n"); break;
	case 0x63: fprintf (stderr, "dBASE IV SQL system files, no memo\n"); break;
	case 0x83: fprintf (stderr, "FoxBASE+/dBASE III PLUS, with memo\n"); break;
	case 0x8B: fprintf (stderr, "dBASE IV with memo\n"); break;
	case 0xCB: fprintf (stderr, "dBASE IV SQL table files, with memo\n"); break;
	case 0xF5: fprintf (stderr, "FoxPro 2.x (or earlier) with memo\n"); break;
	case 0xFB: fprintf (stderr, "FoxBASE\n"); break;
	default:
		fprintf (stderr, "unknown 0x%hhx\n", hdr[0]);
	}

	x->records     = GSF_LE_GET_GUINT32 (hdr + 4);
	x->headerlen   = GSF_LE_GET_GUINT16 (hdr + 8);
	x->fieldlen    = GSF_LE_GET_GUINT16 (hdr + 10);
#if XBASE_DEBUG > 0
	fprintf (stderr, "Last update (YY/MM/DD):\t%2hhd/%2hhd/%2hhd\n",hdr[1],hdr[2],hdr[3]); /* Y2K ?!? */
	fprintf (stderr, "Records:\t%u\n", x->records);
	fprintf (stderr, "Header length:\t%u\n", x->headerlen);
	fprintf (stderr, "Record length:\t%u\n", x->fieldlen);
	fprintf (stderr, "Reserved:\t%d\n", GSF_LE_GET_GUINT16 (hdr + 12));
	fprintf (stderr, "Incomplete transaction:\t%hhd\n", hdr[14]);
	fprintf (stderr, "Encryption flag:\t%d\n", hdr[15]);
	fprintf (stderr, "Free record thread:\t%u\n", GSF_LE_GET_GUINT32 (hdr + 16));
	fprintf (stderr, "Reserved (multi-user):\t%" G_GINT64_FORMAT "\n",
		 GSF_LE_GET_GUINT64(hdr + 20));
	fprintf (stderr, "MDX flag:\t%d\n", hdr[28]); /* FIXME: decode */
	fprintf (stderr, "Reserved:\t%d\n", GSF_LE_GET_GUINT16 (hdr + 30));
	fprintf (stderr, "Language driver (code page):\t");
#endif
	x->char_map = (GIConv)-1;
	for (i = 0; codepages[i].id != 0 ; i++)
		if (codepages[i].id == hdr[29]) {
			x->char_map = gsf_msole_iconv_open_for_import (codepages[i].codepage);
			d (1, fprintf (stderr, "%s (%d)\n",
				       codepages[i].name, codepages[i].codepage););
			break;
		}
	if (x->char_map == (GIConv)-1) {
#if XBASE_DEBUG > 0
		fprintf (stderr, "unknown 0x%x\n!\n", hdr[29]);
#endif
		g_warning ("File has unknown or missing code page information (%x)",
			   hdr[29]);
		/* Got any better idea?  */
		x->char_map = g_iconv_open ("UTF-8", "ISO-8859-1");
	}
}

static XBfield *
xbase_field_new (XBfile *file)
{
	XBfield *field;
	guint8   buf[33];
	char *p;
	if (gsf_input_read (file->input, 2, buf) == NULL) { /* 1 byte out ? */
		g_warning ("xbase_field_new: fread error");
		return NULL;
	} else if (buf[0] == 0x0D || buf[0] == 0) { /* field array terminator */
		file->offset = gsf_input_tell (file->input);
		if (buf[0] == 0x00 && buf[1] == 0x0D) { /* FIXME: crude test, not in spec */
			if (gsf_input_seek (file->input, 263, G_SEEK_CUR)) /* skip DBC */
				g_warning ("xbase_field_new: fseek error");
		}
		return NULL;
	} else if (gsf_input_read (file->input, 30, buf+2) == NULL) {
		g_warning ("Field descriptor short");
		return NULL;
	}
#if XBASE_DEBUG > 0
	buf[32] = 0;
	fprintf (stderr, "Field:\t'%s'\n", buf);
#endif

	field = g_new (XBfield, 1);
	field->len = buf[16];

	strncpy(field->name, buf, 10);
	field->name[10] = '\0';
	if ((p = strchr (field_types, field->type = buf[11])) == NULL)
		g_warning ("Unrecognised field type '%c'", field->type);
#if XBASE_DEBUG > 0
	else
		fprintf (stderr, "Type:\t%c (%s)\n", field->type,
			field_type_descriptions [p-field_types]);
	fprintf (stderr, "Data address:\t0x%.8X\n", GSF_LE_GET_GUINT32 (buf + 12));
	fprintf (stderr, "Length:\t%d\n", field->len);
	fprintf (stderr, "Decimal count:\t%d\n", buf[17]);
#endif
	if (file->fields) {
		XBfield *tmp = file->format[file->fields-1];
		field->pos = tmp->pos + tmp->len;
	} else
		field->pos = 0;

	field->fmt = (field->type == 'D') ? go_format_default_date () : NULL;

	return field; /* FIXME: use more of buf if needed ? */
}

XBfile *
xbase_open (GsfInput *input, ErrorInfo **ret_error)
{
	XBfile *ans;
	XBfield *field;

	*ret_error = NULL;

	ans = g_new (XBfile, 1);
	ans->input = input;

	xbase_read_header (ans, ret_error);
	if (*ret_error) {
		g_free (ans);
		return NULL;
	}

	ans->fields = 0;
	ans->format = NULL;
	while (ans->fields < (gnm_sheet_get_max_cols (NULL)-1) && (field = xbase_field_new (ans)) != NULL) {
		ans->format = g_renew (XBfield *, ans->format, ans->fields + 1);
		/* FIXME: allocate number of field formats from file size? */
		ans->format[ans->fields++] = field;
	}
	return ans;
}

void
xbase_close (XBfile *x)
{
	unsigned i;

	for (i = 0; i < x->fields; i++) {
		XBfield *field = x->format[i];
		if (field->fmt != NULL)
			go_format_unref (field->fmt);
		g_free (field);
	}
	gsf_iconv_close (x->char_map);
	g_free (x->format);
	g_free (x);
}

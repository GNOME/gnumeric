/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include "xbase.h"

#include <format.h>
#include <formats.h>
#include <gutils.h>
#include <io-context.h>

#include <string.h>
#include <gsf/gsf-input.h>
#include <gsf/gsf-utils.h>

#define XBASE_DEBUG 0

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
	offset = (offset-1) * record->file->fieldlen + record->file->headerlen;
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

static gboolean
xbase_read_header (XBfile *x)
{
	int cp;
	guint8 hdr[32];

	if (gsf_input_read (x->input, 32, hdr) == NULL) {
		g_warning ("Header short");
		return TRUE;
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
	fprintf (stderr, "Language driver (code page):\t");
	switch (hdr[29]) {
	case 0x01: cp = 437;
		fprintf (stderr, "U.S. MS-DOS (%d)\n", cp);
		break;
	case 0x69: cp = 620;
		fprintf (stderr, "Mazovia (Polish) MS-DOS (%d)\n", cp);
		break;
	case 0x6A: cp = 737;
		fprintf (stderr, "Greek MS-DOS (437G) (%d)\n", cp);
		break;
	case 0x02: cp = 850;
		fprintf (stderr, "International MS-DOS (%d)\n", cp);
		break;
	case 0x64: cp = 852;
		fprintf (stderr, "Eastern European MS-DOS (%d)\n", cp);
		break;
	case 0x6B: cp = 857;
		fprintf (stderr, "Turkish MS-DOS (%d)\n", cp);
		break;
	case 0x67: cp = 861;
		fprintf (stderr, "Icelandic MS-DOS (%d)\n", cp);
		break;
	case 0x66: cp = 865;
		fprintf (stderr, "Nordic MS-DOS (%d)\n", cp);
		break;
	case 0x65: cp = 866;
		fprintf (stderr, "Russian MS-DOS (%d)\n", cp);
		break;
	case 0x7C: cp = 874;
		fprintf (stderr, "Thai Windows (%d)\n", cp);
		break;
	case 0x68: cp = 895;
		fprintf (stderr, "Kamenicky (Czech) MS-DOS (%d)\n", cp);
		break;
	case 0x7B: cp = 932;
		fprintf (stderr, "Japanese Windows (%d)\n", cp);
		break;
	case 0x7A: cp = 936;
		fprintf (stderr, "Chinese (PRC, Singapore) Windows (%d)\n", cp);
		break;
	case 0x79: cp = 949;
		fprintf (stderr, "Korean Windows (%d)\n", cp);
		break;
	case 0x78: cp = 950;
		fprintf (stderr, "Chinese (Hong Kong SAR, Taiwan) Windows (%d)\n", cp);
		break;
	case 0xC8: cp = 1250;
		fprintf (stderr, "Eastern European Windows (%d)\n", cp);
		break;
	case 0xC9: cp = 1251;
		fprintf (stderr, "Russian Windows (%d)\n", cp);
		break;
	case 0x03: cp = 1252;
		fprintf (stderr, "Windows ANSI (%d)\n", cp);
		break;
	case 0xCB: cp = 1253;
		fprintf (stderr, "Greek Windows (%d)\n", cp);
		break;
	case 0xCA: cp = 1254;
		fprintf (stderr, "Turkish Windows (%d)\n", cp);
		break;
	case 0x7D: cp = 1255;
		fprintf (stderr, "Hebrew Windows (%d)\n", cp);
		break;
	case 0x7E: cp = 1256;
		fprintf (stderr, "Arabic Windows (%d)\n", cp);
		break;
	case 0x04: cp = 10000;
		fprintf (stderr, "Standard Macintosh (%d)\n", cp);
		break;
	case 0x98: cp = 10006;
		fprintf (stderr, "Greek Macintosh (%d)\n", cp);
		break;
	case 0x96: cp = 10007;
		fprintf (stderr, "Russian Macintosh (%d)\n", cp);
		break;
	case 0x97: cp = 10029;
		fprintf (stderr, "Macintosh EE (%d)\n", cp);
		break;
	default:
		fprintf (stderr, "unknown 0x%hhx\n!\n", hdr[29]);
	}
	fprintf (stderr, "Reserved:\t%d\n", GSF_LE_GET_GUINT16 (hdr + 30));
#endif
	return FALSE;
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

	field->fmt = (field->type == 'D') ? style_format_default_date () : NULL;

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

	xbase_read_header (ans); /* FIXME: Clean up xbase_read_header
				  * and handle errors */
	ans->fields = 0;
	ans->format = NULL;
	while ((field = xbase_field_new (ans)) != NULL) {
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
			style_format_unref (field->fmt);
		g_free (field);
	}
	g_free (x->format);
	g_free (x);
}

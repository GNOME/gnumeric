#include <config.h>
#include "xbase.h"

#include <format.h>
#include <gutils.h>
#include <io-context.h>

#include <string.h>

#define XBASE_DEBUG 0

static const char *field_types = "CNLDMF?BGPYTI";

#if XBASE_DEBUG > 0
static const char *field_type_descriptions [] = { /* FIXME: fix array size from field_types*/
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
	ans->data = g_strnfill (file->fieldlen, '?'); /* FIXME : just for testing */
	record_seek (ans, SEEK_SET, 1);
	return ans;
}

/**
 * Position record at requested row, and load raw data.  Returns FALSE on
 * invalid row, file error, or invalid whence (same values as in fseek).
 */
gboolean
record_seek (XBrecord *record, int whence, glong row)
{
	long offset;
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
	offset = (offset-1) * record->file->fieldlen + record->file->offset;
	if (fseek (record->file->f, offset, SEEK_SET) ||
	    fread(record->data, record->file->fieldlen, 1, record->file->f) != 1)
		return FALSE;
	return TRUE;
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
guint8 *
record_get_field (const XBrecord *record, guint num)
{
	if (num > record->file->fields)
		return NULL;
	return record->data + record->file->format[num-1]->pos;
}

static gboolean
xbase_read_header (XBfile *x)
{
	guint8 hdr[32];
	if (fread (hdr, 1, 32, x->f) != 32) {
		g_warning ("Header short");
		return TRUE;
	}
	fprintf (stderr, "Version:\t");
	switch (hdr[0]) { /* FIXME: assuming dBASE III+, not IV */
	case 0x02:
		fprintf (stderr, "FoxBase\n");
		break;
	case 0x03:
		fprintf (stderr, "File without DBT\n");
		break;
	case 0x30:
		fprintf (stderr, "Visual FoxPro\n");
		break;
	case 0x83:
		fprintf (stderr, "File with DBT\n"); /* bits: 0-3 version, 3-5 SQL, 7 DBT flag */
		break;
	default:
		fprintf (stderr, "unknown!\n");
	}
	x->records  = gnumeric_get_le_uint32 (hdr + 4);
	x->fieldlen = gnumeric_get_le_uint16 (hdr + 10);
#if XBASE_DEBUG > 0
	fprintf (stderr, "Last update (YY/MM/DD):\t%2d/%2d/%2d\n",hdr[1],hdr[2],hdr[3]); /* Y2K ?!? */
	fprintf (stderr, "Records:\t%u\n", x->records);
	fprintf (stderr, "Header length:\t%d\n", gnumeric_get_le_uint16 (hdr + 8));
	fprintf (stderr, "Record length:\t%d\n", x->fieldlen);
	fprintf (stderr, "Reserved:\t%d\n", gnumeric_get_le_uint16 (hdr + 12));
	fprintf (stderr, "Incomplete transaction:\t%d\n", hdr[14]);
	fprintf (stderr, "Encryption flag:\t%d\n", hdr[15]);
	fprintf (stderr, "Free record thread:\t%u\n", gnumeric_get_le_uint32 (hdr + 16));
#ifdef THIS_IS_BOGUS
	fprintf (stderr, "Reserved (multi-user):\t%lu\n", GUINT64_FROM_LE((guint64)hdr[20])); /* FIXME: printf needs to support 64-bit integers */
#endif
	fprintf (stderr, "MDX flag:\t%d\n", hdr[28]); /* FIXME: decode */
	fprintf (stderr, "Language driver (code page):\t");
	switch (hdr[29]) {
	case 0x01:
		fprintf (stderr, "DOS USA (437)\n");
		break;
	case 0x02:
		fprintf (stderr, "DOS Multilingual (850)\n");
		break;
	case 0x03:
		fprintf (stderr, "Windows ANSI (1251)\n");
		break;
	case 0xC8:
		fprintf (stderr, "Windows EE (1250)\n");
		break;
	case 0x64:
		fprintf (stderr, "EE MS-DOS (852)\n");
		break;
	case 0x66:
		fprintf (stderr, "Russian MS-DOS (866)\n");
		break;
	case 0x65:
		fprintf (stderr, "Nordic MS-DOS (865)\n");
		break;
	default:
		fprintf (stderr, "unknown!\n");
	}
	fprintf (stderr, "Reserved:\t%d\n", gnumeric_get_le_uint16 (hdr + 30));
#endif
	return FALSE;
}

static XBfield *
xbase_read_field (XBfile *file)
{
	XBfield *ans;
	guint8 buf[33];
	char *p;
	if (fread(buf, sizeof(buf[0]), 2, file->f) != 2) { /* 1 byte out ? */
		g_warning ("xbase_read_field: fread error");
		return NULL;
	} else if (buf[0] == 0x0D || buf[0] == 0) { /* field array terminator */
		if (buf[1] == 0) { /* FIXME: crude test, not in spec */
			if (fseek(file->f, 263, SEEK_CUR)) /* skip DBC */
				g_warning ("xbase_read_field: fseek error");
		}
		file->offset = ftell (file->f);
		return NULL;
	} else if (fread(&buf[2], sizeof(buf[0]), 30, file->f) != 30) {
		g_warning ("Field descriptor short");
		return NULL;
	}
#if XBASE_DEBUG > 0
	buf[32] = 0;
	fprintf (stderr, "Field:\t'%s'\n", buf);
#endif

	ans = g_new (XBfield, 1);
	ans->len = buf[16];

	strncpy(ans->name, buf, 10);
	ans->name[10] = '\0';
	if ((p = strchr (field_types, ans->type = buf[11])) == NULL)
		g_warning ("Unrecognised field type '%c'", ans->type);
#if XBASE_DEBUG > 0
	else
		fprintf (stderr, "Type:\t%c (%s)\n", ans->type,
			field_type_descriptions [p-field_types]);
	fprintf (stderr, "Data address:\t0x%.8X\n", gnumeric_get_le_uint32 (buf + 12));
	fprintf (stderr, "Length:\t%d\n", ans->len);
	fprintf (stderr, "Decimal count:\t%d\n", buf[17]);
#endif
	if (file->fields) {
		XBfield *tmp = file->format[file->fields-1];
		ans->pos = tmp->pos + tmp->len;
	} else
		ans->pos = 0;
	return ans; /* FIXME: use more of buf if needed ? */
}

XBfile *
xbase_open (const char *filename, ErrorInfo **ret_error)
{
	FILE *f;
	XBfile *ans;
	XBfield *field;

	*ret_error = NULL;
	if ((f = fopen (filename, "rb")) == NULL) {
		*ret_error = error_info_new_from_errno ();
		return NULL;
	}

	ans = g_new (XBfile, 1);
	ans->f = f;
	ans->offset = 0;

	xbase_read_header (ans); /* FIXME: Clean up xbase_read_header
				  * and handle errors */
	ans->fields = 0;
	ans->format = NULL;
	while ((field = xbase_read_field (ans)) != NULL) {
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

	fprintf (stderr, "Closing Xbase file\n");
	fclose (x->f);
	for (i = 0; i < x->fields; i++)
		g_free (x->format[i]);
	g_free (x->format);
	g_free (x);
}

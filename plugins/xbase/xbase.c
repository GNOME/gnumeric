#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <config.h>
#include <stdio.h>
#include <ctype.h>
#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "format.h"
#include "color.h"
#include "sheet-object.h"

#include "xbase.h"

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
	if (offset < 1 || offset > record->file->records)
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
	printf ("Version:\t");
	switch (hdr[0]) { /* FIXME: assuming dBASE III+, not IV */
	case 0x02:
		printf ("FoxBase\n");
		break;
	case 0x03:
		printf ("File without DBT\n");
		break;
	case 0x30:
		printf ("Visual FoxPro\n");
		break;
	case 0x83:
		printf ("File with DBT\n"); /* bits: 0-3 version, 3-5 SQL, 7 DBT flag */
		break;
	default:
		printf ("unknown!\n");
	}
	x->records  = GUINT32_FROM_LE((guint32)hdr[4]);
	x->fieldlen = GUINT16_FROM_LE((guint16)hdr[10]);
#if XBASE_DEBUG > 0
	printf ("Last update (YY/MM/DD):\t%2d/%2d/%2d\n",hdr[1],hdr[2],hdr[3]); /* Y2K ?!? */
	printf ("Records:\t%u\n", x->records);
	printf ("Header length:\t%d\n", GUINT16_FROM_LE((guint16)hdr[8]));
	printf ("Record length:\t%d\n", x->fieldlen);
	printf ("Reserved:\t%d\n", GUINT16_FROM_LE((guint16)hdr[12]));
	printf ("Incomplete transaction:\t%d\n", hdr[14]);
	printf ("Encryption flag:\t%d\n", hdr[15]);
	printf ("Free record thread:\t%u\n", GUINT32_FROM_LE((guint32)hdr[16]));
	printf ("Reserved (multi-user):\t%lu\n", GUINT64_FROM_LE((guint64)hdr[20])); /* FIXME: printf needs to support 64-bit integers */
	printf ("MDX flag:\t%d\n", hdr[28]); /* FIXME: decode */
	printf ("Language driver (code page):\t");
	switch (hdr[29]) {
	case 0x01:
		printf ("DOS USA (437)\n");
		break;
	case 0x02:
		printf ("DOS Multilingual (850)\n");
		break;
	case 0x03:
		printf ("Windows ANSI (1251)\n");
		break;
	case 0xC8:
		printf ("Windows EE (1250)\n");
		break;
	case 0x64:
		printf ("EE MS-DOS (852)\n");
		break;
	case 0x66:
		printf ("Russian MS-DOS (866)\n");
		break;
	case 0x65:
		printf ("Nordic MS-DOS (865)\n");
		break;
	default:
		printf ("unknown!\n");
	}
	printf ("Reserved:\t%d\n", GUINT16_FROM_LE((guint16)hdr[30]));
#endif
	return FALSE;
}

static XBfield *
xbase_read_field (XBfile *file)
{
	XBfield *ans = g_new (XBfield, 1), *tmp;
	guint8 buf[32];
	char *p;
	if (fread(buf, sizeof(buf[0]), 2, file->f) != 2) { /* 1 byte out ? */
		g_warning ("xbase_read_field: fread error");
		return NULL;
	} else if (buf[0] == 0x0D) { /* field array terminator */
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
	printf ("Field:\t'%s'\n", buf);
#endif
	strncpy(ans->name, buf, 10);
	ans->name[10] = '\0';
	if ((p = strchr (field_types, ans->type = buf[11])) == NULL)
		g_warning ("Unrecognised field type '%c'", ans->type);
#if XBASE_DEBUG > 0
	else
		printf ("Type:\t%c (%s)\n", ans->type,
			field_type_descriptions [p-field_types]);
	printf ("Data address:\t0x%.8X\n", GUINT32_FROM_LE((guint32)buf[12]));
	printf ("Length:\t%d\n", buf[16]);
	printf ("Decimal count:\t%d\n", buf[17]);
#endif
	ans->len = buf[16];
	if (file->fields) {
		tmp = file->format[file->fields-1];
		ans->pos = tmp->pos + tmp->len;
	} else
		ans->pos = 0;
	return ans; /* FIXME: use more of buf if needed ? */
}

XBfile *
xbase_open (const char *filename)
{
	XBfile *ans = g_new (XBfile, 1);
	XBfield *field;
	printf ("Opening Xbase file \"%s\"...\n", filename);
	if ((ans->f = fopen (filename, "rb")) == NULL) {
		g_warning ("Error opening \"%s\"", filename);
		xbase_close (ans);
		return NULL;
	}
	xbase_read_header (ans);
	ans->fields = 0;
	ans->format = NULL;
	while ((field = xbase_read_field (ans)) != NULL) {
		ans->format = g_realloc (ans->format,
					 sizeof(ans->format)*++ans->fields);
		/* FIXME: allocate number of field formats from file size? */
		ans->format[ans->fields-1] = field;
	}
	return ans;
}

void
xbase_close (XBfile *x)
{
	printf("Closing Xbase file\n");
	fclose (x->f);
	g_free (x->format);
	g_free (x);
}

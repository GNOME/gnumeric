/**
 * ms-summary.c: MS Excel support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 **/

#include <stdio.h>

#include <glib.h>

#include "ms-ole.h"
#include "ms-biff.h"
#include "ms-summary.h"

typedef struct _MsOleSummaryHeader MsOleSummaryHeader;
typedef struct _MsOleSummaryRecord MsOleSummaryRecord;
typedef guint32 MsOleSummaryFileTime;

#define SUMMARY_DEBUG 0

#define RECORD_HEADER 10
#define HEADER_LEN    0x38

/* LONG  = 4 bytes */
/* DWORD = 2 bytes ( signed ? ) */

static gchar *sum_names[] = {
	"Unknown",				/* 0x0 */
	"Codepage Property",                    /* 0x1 */
	"Title",                                /* 0x2 */
	"Subject",                              /* 0x3 */
	"Author",                               /* 0x4 */
	"Keywords",                             /* 0x5 */
	"Comments",                             /* 0x6 */
	"Template",                             /* 0x7 */
	"Last Saved By",                        /* 0x8 */
	"Revision Number",                      /* 0x9 */
	"Unknown",                              /* 0xa */
	"Last printed summary properties",      /* 0xb */
	"Create time/date",                     /* 0xc */
	"Last save time/date",                  /* 0xd */
	"Page count",                           /* 0xe */
	"Word count",                           /* 0xf */
	"Character count",                      /* 0x10 */
	"Unknown",                              /* 0x11 */
	"Creating Application",                 /* 0x12 */
	"Security"                              /* 0x13 */
};

static SummaryItemBuiltin
excel_to_gnumeric (guint32 type)
{
	switch (type)
	{
	case 2:
		return SUMMARY_I_TITLE;
	case 3:
		return SUMMARY_I_SUBJECT;
	case 4:
		return SUMMARY_I_AUTHOR;
	case 5:
		return SUMMARY_I_KEYWORDS;
	case 6:
		return SUMMARY_I_COMMENTS;
	case 0x12:
		return SUMMARY_I_APP;
	default:
		return SUMMARY_I_MAX;
	}
}

static SummaryItem *
read_records (MsOleStream *s, guint32 type)
{
	guint8       data[64];
	guint8      *mem;
	gint32       len, rec_type;
	SummaryItem *sit;
	SummaryItemBuiltin t;
	char *name;
       
	t = excel_to_gnumeric (type);

	if (t != SUMMARY_I_MAX)
		name = summary_item_name [t];
	else {
		if (type < sizeof (sum_names))
			name = sum_names[type];
		else
			name = "Error";
	}
	
	if (!s->read_copy (s, data, 4)) {
#if SUMMARY_DEBUG > 0
		printf ("Serious error reading type\n");
#endif
		return NULL;
	}

	rec_type = MS_OLE_GET_GUINT32 (data);
#if SUMMARY_DEBUG > 0
	printf ("Next record at 0xx : '%s' : type 0x%x\n",
		sum_names[type], rec_type);
#endif
       
	switch (rec_type) {
	case 0x1e: /* String */
	{
		if (!s->read_copy (s, data, 4))
			return NULL;

		len = MS_OLE_GET_GUINT32 (data);

		mem = g_malloc (len+1);
		if (!s->read_copy (s, mem, len))
			return NULL;
/*		dump (mem, len); */

#if SUMMARY_DEBUG > 1
		printf ("string '%s'\n", mem);
#endif
		sit = summary_item_new_string (name, mem);
		g_free (mem);
		return sit;
	}


	case 0x3:
		if (!s->read_copy (s, data, 4))
			return NULL;
#if SUMMARY_DEBUG > 1
		printf ("Integer : 0x%x\n", MS_OLE_GET_GUINT32 (data));
#endif
		return summary_item_new_int (name, MS_OLE_GET_GUINT32 (data));


	case 0x40:
		if (!s->read_copy (s, data, 8))
			return NULL;
#if SUMMARY_DEBUG > 1
		printf ("Timestamp : 0x%x%x\n", MS_OLE_GET_GUINT32 (data + 4),
			MS_OLE_GET_GUINT32 (data));
#endif
		return summary_item_new_int (name, MS_OLE_GET_GUINT32 (data + 4));


	default:
#if SUMMARY_DEBUG > 0
		printf ("Unknown type:\n");
		g_return_val_if_fail (s->read_copy (s, data, 32), NULL);
		dump (data, 32);
#endif
		break;
	}
	return NULL;
}

void
ms_summary_read (MsOle *f, SummaryInfo *sin)
{
/*	MsOleSummary *si = ms_ole_summary_open (f);
	Broken it temporarily... :-)
	ms_ole_summary_destroy (f); */
}


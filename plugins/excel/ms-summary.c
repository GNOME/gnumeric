/**
 * ms-summary.c: MS Excel support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 **/

#include <stdio.h>

#include <glib.h>

#include "ms-ole.h"
#include "ms-ole-summary.h"
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

typedef struct {
	MsOleSummaryPID    excel;
	SummaryItemBuiltin gnumeric;
} mapping_t;

mapping_t excel_to_gnum_mapping[] =
{ /* Needs beefing up */
	{ MS_OLE_SUMMARY_TITLE,    SUMMARY_I_TITLE },
	{ MS_OLE_SUMMARY_SUBJECT,  SUMMARY_I_SUBJECT },
	{ MS_OLE_SUMMARY_AUTHOR,   SUMMARY_I_AUTHOR },
	{ MS_OLE_SUMMARY_KEYWORDS, SUMMARY_I_KEYWORDS },
	{ MS_OLE_SUMMARY_COMMENTS, SUMMARY_I_COMMENTS },
	{ MS_OLE_SUMMARY_APPNAME,  SUMMARY_I_APP }
};

/*static SummaryItemBuiltin
excel_to_gnumeric (guint32 type)
{
	gint i;
	for (i = 0; i < sizeof (excel_to_gnum_mapping)/sizeof(mapping_t); i++) {
		if (excel_to_gnum_mapping[i].excel == type)
			return excel_to_gnum_mapping[i].gnumeric;
	}
	return SUMMARY_I_MAX;
}*/

static void
read_summary_items (SummaryInfo *sin, MsOleSummary *si)
{
	gint         i;
	SummaryItem *sit;
	gboolean     ok;

	for (i = 0; i < sizeof (excel_to_gnum_mapping)/sizeof(mapping_t); i++) {
		MsOleSummaryPID  p = excel_to_gnum_mapping[i].excel;
		gchar           *name;

		sit = NULL;
		name = summary_item_name[excel_to_gnum_mapping[i].gnumeric];
		switch (MS_OLE_SUMMARY_TYPE (p)) {

		case MS_OLE_SUMMARY_TYPE_STRING:
		{
			gchar *val = ms_ole_summary_get_string (si, p, &ok);
			if (ok) {
				sit = summary_item_new_string (name, val);
				g_free (val);
			}
			break;
		}

		case MS_OLE_SUMMARY_TYPE_LONG:
		{
			guint32 val = ms_ole_summary_get_long (si, p, &ok);
			if (ok)
				sit = summary_item_new_int (name, val);
			break;
		}

		default:
			g_warning ("Unsupported summary type");
			break;
		}
		if (sit)
			summary_info_add (sin, sit);
	}
}

void
ms_summary_read (MsOle *f, SummaryInfo *sin)
{
	MsOleSummary *si = ms_ole_summary_open (f);
	if (si) {
		read_summary_items (sin, si);
		ms_ole_summary_close (si);
	}
}


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
#include "summary.h"

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


/**
 *  sum_name_to_excel
 *
 *  This function takes as input the name from a SummaryItem record and
 *  converts it back to an MsOleSummaryPID.
 *
**/
static MsOleSummaryPID
sum_name_to_excel (gchar *name)
{
	gint	 i, j;
	/*
	 *  First find the name in the summary_item_name array.
	 *  The index corresponds to a SummaryItemBuiltin type defined
	 *  in excel_to_gnum_mapping[].  What we want is the corresponding
	 *  MsOleSummaryPID in that same array.
	 */
	for (i = 0; i < sizeof (summary_item_name); i++) {
		if (g_strcasecmp (summary_item_name[i], name) == 0) {
			for (j = 0; j < sizeof (excel_to_gnum_mapping) / sizeof (mapping_t); j++) {
				if (excel_to_gnum_mapping[j].gnumeric == i)
					return excel_to_gnum_mapping[j].excel;
			}

			g_warning ("sum_name_to_excel: gnumeric summary type not found - %d\n", i);
			return (MsOleSummaryPID)-1;
		}
	}
	
	g_warning ("sum_name_to_excel: summary name not found - %s\n", name);
	return (MsOleSummaryPID)-1;
}

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


static void
set_summary_item (SummaryItem *s_item, MsOleSummary *ms_sum)
{
	MsOleSummaryPID	pid;
	
	pid = sum_name_to_excel (s_item->name);
	g_return_if_fail (pid != -1);
	
	switch (s_item->type) {
	
	case SUMMARY_STRING:
		ms_ole_summary_set_string (ms_sum, pid, s_item->v.txt);
		break;
	
	case SUMMARY_INT:
		ms_ole_summary_set_long (ms_sum, pid, s_item->v.i);
		break;
	
	case SUMMARY_TIME:
		/* ms_ole_summary_set_time (ms_sum, pid, s_item->v.time); */
		break;
	
	default:
		g_warning ("Unsupported summary type: %d", s_item->type);
		break;
	
	}
	
}


void
ms_summary_write (MsOle *f, SummaryInfo *sin)
{
	GList		*si_list;
	MsOleSummary	*si;
	
	if (f == NULL)
	{
		g_warning ("ms_summary_write: no file to write to.\n");
		return;
	}
	
	if (sin == NULL) {
		g_warning ("ms_summary_write: no summary information to write.\n");
		return;
	}

	si = ms_ole_summary_create (f);
	if (si == NULL) {
		g_warning ("ms_summary_write: summary NOT created.\n");
		return;
	}

	si_list = summary_info_as_list (sin);
	if (si_list == NULL)
	{
		g_warning ("ms_summary_write: No summary list.\n");
	}

	g_list_foreach (si_list, (GFunc)set_summary_item, si);
	
	ms_ole_summary_close (si);
	
}


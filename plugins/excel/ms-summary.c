/**
 * ms-summary.c: MS Excel support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 1998, 1999, 2000 Michael Meeks
 **/

#include <stdio.h>

#include <config.h>
#include <glib.h>
#include <libole2/ms-ole.h>
#include <libole2/ms-ole-summary.h>

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


/*
 *  MsOleSummaryPID	is defined in plugins/excel/libole2/ms-ole-summary.h
 *  MsOlePropertySetID	is defined in plugins/excel/libole2/ms-ole-summary.h
 *  SummaryItemBuiltin	is defined in src/summary.h
 */
typedef struct {
	MsOleSummaryPID    excel;
	MsOlePropertySetID ps_id;
	SummaryItemBuiltin gnumeric;
} mapping_t;

mapping_t excel_to_gnum_mapping[] =
{ /* Needs beefing up */
	{ MS_OLE_SUMMARY_CODEPAGE,     MS_OLE_PS_SUMMARY_INFO,           SUMMARY_I_CODEPAGE },
	{ MS_OLE_SUMMARY_TITLE,        MS_OLE_PS_SUMMARY_INFO,           SUMMARY_I_TITLE },
	{ MS_OLE_SUMMARY_SUBJECT,      MS_OLE_PS_SUMMARY_INFO,           SUMMARY_I_SUBJECT },
	{ MS_OLE_SUMMARY_AUTHOR,       MS_OLE_PS_SUMMARY_INFO,           SUMMARY_I_AUTHOR },
	{ MS_OLE_SUMMARY_KEYWORDS,     MS_OLE_PS_SUMMARY_INFO,           SUMMARY_I_KEYWORDS },
	{ MS_OLE_SUMMARY_COMMENTS,     MS_OLE_PS_SUMMARY_INFO,           SUMMARY_I_COMMENTS },
	{ MS_OLE_SUMMARY_TEMPLATE,     MS_OLE_PS_SUMMARY_INFO,	         SUMMARY_I_TEMPLATE },
	{ MS_OLE_SUMMARY_LASTAUTHOR,   MS_OLE_PS_SUMMARY_INFO,	         SUMMARY_I_LASTAUTHOR },
	{ MS_OLE_SUMMARY_REVNUMBER,    MS_OLE_PS_SUMMARY_INFO,	         SUMMARY_I_REVNUMBER },
	{ MS_OLE_SUMMARY_LASTPRINTED,  MS_OLE_PS_SUMMARY_INFO,           SUMMARY_I_LASTPRINTED },
	{ MS_OLE_SUMMARY_CREATED,      MS_OLE_PS_SUMMARY_INFO,           SUMMARY_I_CREATED },
	{ MS_OLE_SUMMARY_LASTSAVED,    MS_OLE_PS_SUMMARY_INFO,           SUMMARY_I_LASTSAVED },
	{ MS_OLE_SUMMARY_PAGECOUNT,    MS_OLE_PS_SUMMARY_INFO,           SUMMARY_I_PAGECOUNT },
	{ MS_OLE_SUMMARY_WORDCOUNT,    MS_OLE_PS_SUMMARY_INFO,           SUMMARY_I_WORDCOUNT },
	{ MS_OLE_SUMMARY_CHARCOUNT,    MS_OLE_PS_SUMMARY_INFO,           SUMMARY_I_CHARCOUNT },
	{ MS_OLE_SUMMARY_APPNAME,      MS_OLE_PS_SUMMARY_INFO,           SUMMARY_I_APP },
	{ MS_OLE_SUMMARY_SECURITY,     MS_OLE_PS_SUMMARY_INFO,           SUMMARY_I_SECURITY },

	{ MS_OLE_SUMMARY_CATEGORY,     MS_OLE_PS_DOCUMENT_SUMMARY_INFO,  SUMMARY_I_CATEGORY },
	{ MS_OLE_SUMMARY_PRESFORMAT,   MS_OLE_PS_DOCUMENT_SUMMARY_INFO,  SUMMARY_I_PRESFORMAT },
	{ MS_OLE_SUMMARY_BYTECOUNT,    MS_OLE_PS_DOCUMENT_SUMMARY_INFO,  SUMMARY_I_BYTECOUNT },
	{ MS_OLE_SUMMARY_LINECOUNT,    MS_OLE_PS_DOCUMENT_SUMMARY_INFO,  SUMMARY_I_LINECOUNT },
	{ MS_OLE_SUMMARY_PARCOUNT,     MS_OLE_PS_DOCUMENT_SUMMARY_INFO,  SUMMARY_I_PARCOUNT  },
	{ MS_OLE_SUMMARY_SLIDECOUNT,   MS_OLE_PS_DOCUMENT_SUMMARY_INFO,  SUMMARY_I_SLIDECOUNT },
	{ MS_OLE_SUMMARY_NOTECOUNT,    MS_OLE_PS_DOCUMENT_SUMMARY_INFO,  SUMMARY_I_NOTECOUNT },
	{ MS_OLE_SUMMARY_HIDDENCOUNT,  MS_OLE_PS_DOCUMENT_SUMMARY_INFO,  SUMMARY_I_HIDDENCOUNT },
	{ MS_OLE_SUMMARY_MMCLIPCOUNT,  MS_OLE_PS_DOCUMENT_SUMMARY_INFO,  SUMMARY_I_MMCLIPCOUNT },
	{ MS_OLE_SUMMARY_SCALE,        MS_OLE_PS_DOCUMENT_SUMMARY_INFO,  SUMMARY_I_SCALE },
	{ MS_OLE_SUMMARY_MANAGER,      MS_OLE_PS_DOCUMENT_SUMMARY_INFO,  SUMMARY_I_MANAGER },
	{ MS_OLE_SUMMARY_COMPANY,      MS_OLE_PS_DOCUMENT_SUMMARY_INFO,  SUMMARY_I_COMPANY },
	{ MS_OLE_SUMMARY_LINKSDIRTY,   MS_OLE_PS_DOCUMENT_SUMMARY_INFO,  SUMMARY_I_LINKSDIRTY }
};


int sum_name_to_excel (gchar *name, MsOleSummaryPID *pid, MsOlePropertySetID psid);

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
int
sum_name_to_excel (gchar *name, MsOleSummaryPID *pid, MsOlePropertySetID psid)
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
			        if ((excel_to_gnum_mapping[j].ps_id    == psid) &&
				    (excel_to_gnum_mapping[j].gnumeric ==    i)    ) {
					*pid = excel_to_gnum_mapping[j].excel;
					return 1;
				}
			}

			return 0;
		}
	}

	g_warning ("sum_name_to_excel: summary name not found - %s\n", name);
	return 0;
}

static void
read_summary_items (SummaryInfo *sin, MsOleSummary *si, MsOlePropertySetID psid)
{
	gint         i;
	SummaryItem *sit;
	gboolean     ok;

	for (i = 0; i < sizeof (excel_to_gnum_mapping)/sizeof(mapping_t); i++) {
		if (excel_to_gnum_mapping[i].ps_id == psid) {
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

			case MS_OLE_SUMMARY_TYPE_BOOLEAN:
			{
				gboolean val = ms_ole_summary_get_boolean (si, p, &ok);
				if (ok)
					sit = summary_item_new_boolean (name, val);
				break;
			}

			case MS_OLE_SUMMARY_TYPE_SHORT:
			{
				guint16 val = ms_ole_summary_get_short (si, p, &ok);
				if (ok)
					sit = summary_item_new_short (name, val);
				break;
			}

			case MS_OLE_SUMMARY_TYPE_LONG:
			{
				guint32 val = ms_ole_summary_get_long (si, p, &ok);
				if (ok)
					sit = summary_item_new_int (name, val);
				break;
			}

			case MS_OLE_SUMMARY_TYPE_TIME:
			{
				GTimeVal val = ms_ole_summary_get_time (si, p, &ok);
				if (ok)
					sit = summary_item_new_time (name, val);
				break;
			}

			default:
				g_warning ("Unsupported summary type:%#x", p);
				break;
			}

			if (sit)
				summary_info_add (sin, sit);
		}
	}
}

void
ms_summary_read (MsOle *f, SummaryInfo *sin)
{
	/*
	 *  Get all the information from the SummaryInformation stream.
	 */
	MsOleSummary *si = ms_ole_summary_open (f);
	if (si) {
		read_summary_items (sin, si, MS_OLE_PS_SUMMARY_INFO);
		ms_ole_summary_close (si);
	}

	/*
	 *  Get all the information from the DocumentSummaryInformation stream.
	 */
	si = ms_ole_docsummary_open (f);
	if (si)
	{
		read_summary_items (sin, si, MS_OLE_PS_DOCUMENT_SUMMARY_INFO);
		ms_ole_summary_close (si);
	} else {
#if SUMMARY_DEBUG > 0
		printf ("ms_summary_read: Unable to open DocumentSummaryInformation.\n");
#endif
	}
}


static void
set_summary_item (SummaryItem *s_item, MsOleSummary *ms_sum)
{
	gint		 sect;
	MsOleSummaryPID	 pid;

	for (sect = 0; sect < ms_sum->sections->len; sect++) {
		MsOleSummarySection st;

		st = g_array_index (ms_sum->sections, MsOleSummarySection, sect);

		if (sum_name_to_excel (s_item->name, &pid, st.ps_id) != 0) {

			switch (s_item->type) {

			case SUMMARY_STRING:
				ms_ole_summary_set_string (ms_sum, pid, s_item->v.txt);
				break;

			case SUMMARY_BOOLEAN:
				ms_ole_summary_set_boolean (ms_sum, pid, s_item->v.boolean);
				break;

			case SUMMARY_SHORT:
				ms_ole_summary_set_short (ms_sum, pid, s_item->v.short_i);
				break;

			case SUMMARY_INT:
				ms_ole_summary_set_long (ms_sum, pid, s_item->v.i);
				break;

			case SUMMARY_TIME:
				ms_ole_summary_set_time (ms_sum, pid, s_item->v.time);
				break;

			default:
				g_warning ("set_summary_item: Unsupported summary type - %d",
					   s_item->type);
				break;
			}
		}

	}

}


void
ms_summary_write (MsOle *f, SummaryInfo *sin)
{
	GList		*si_list;
	MsOleSummary	*si;

	if (f == NULL) {
		g_warning ("ms_summary_write: no file to write to.\n");
		return;
	}

	if (sin == NULL) {
		g_warning ("ms_summary_write: no summary information to write.\n");
		return;
	}

	/*
	 *  Write out SummaryInformation.
	 */
	si = ms_ole_summary_create (f);
	if (si == NULL) {
		g_warning ("ms_summary_write: summary NOT created.\n");
		return;
	}

	si_list = summary_info_as_list (sin);
	if (si_list == NULL) {
		g_warning ("ms_summary_write: No summary list.\n");
	}

	g_list_foreach (si_list, (GFunc)set_summary_item, si);

	ms_ole_summary_close (si);

	/*
	 *  Write out DocumentSummaryInformation.
	 */
	si = ms_ole_docsummary_create (f);
	if (si == NULL) {
		g_warning ("ms_summary_write: doc summary NOT created.\n");
		return;
	}

	si_list = summary_info_as_list (sin);
	if (si_list == NULL) {
		g_warning ("ms_summary_write: No summary list.\n");
	}

	g_list_foreach (si_list, (GFunc)set_summary_item, si);

	ms_ole_summary_close (si);
}

/**
 * ms-ole-summary.h: MS Office OLE support
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 * From work by:
 *    Caolan McNamara (Caolan.McNamara@ul.ie)
 * Built on work by:
 *    Somar Software's CPPSUM (http://www.somar.com)
 **/

#include <glib.h>
#include "ms-ole.h"
#include "ms-ole-summary.h"

MsOleSummary *
ms_ole_summary_open_stream (MsOleStream *s)
{
	guint8        data[64];
	guint16       byte_order;
	gboolean      panic = FALSE;
	guint32       os_version;
	MsOleSummary *si;
	gint          i;

	g_return_val_if_fail (s != NULL, NULL);

	if (!s->read_copy (s, data, 28))
		return NULL;

	si                = g_new (MsOleSummary, 1);

	si->s             = s;
	byte_order        = MS_OLE_GET_GUINT16(data);
	si->little_endian = TRUE; /* FIXME */
	if (MS_OLE_GET_GUINT16 (data + 2) != 0) /* Format */
		panic     = TRUE;
	os_version        = MS_OLE_GET_GUINT32 (data + 4);

	for (i=0;i<16;i++)
		si->windows_GUID[i] = data[8 + i];

	si->sections      = MS_OLE_GET_GUINT32 (data + 24);

	if (panic) {
		g_free (si);
		return NULL;
	}
	return si;
}

MsOleSummary *
ms_ole_summary_open (MsOle *f)
{
	MsOleStream *s;
	g_return_val_if_fail (f != NULL, NULL);

	s = ms_ole_stream_open_name (ms_ole_get_root (f),
				     "SummaryInformation", 'r');
	if (!s)
		return NULL;

	return ms_ole_summary_open_stream (s);
}

void ms_ole_summary_destroy (MsOleSummary *si)
{
	g_return_if_fail (si != NULL);
	g_return_if_fail (si->s != NULL);

	ms_ole_stream_close (si->s); 
	si->s = NULL;
	g_free (si);
}

/* Ensure that you free these pointers after use */
char *
ms_ole_summary_get_string (MsOleSummary *si, MsOleSummaryPID id,
			   gboolean *not_available)
{
	g_return_val_if_fail (si != NULL, NULL);
	g_return_val_if_fail (not_available != NULL, NULL);
	return NULL;
}

guint32 
ms_ole_summary_get_long (MsOleSummary *si, MsOleSummaryPID id,
			 gboolean *not_available)
{
	g_return_val_if_fail (si != NULL, 0);
	g_return_val_if_fail (not_available != NULL, 0);
	return 0;
}

MsOleSummaryTime *
ms_ole_summary_get_time (MsOleSummary *si, MsOleSummaryPID id,
			 gboolean *not_available)
{
	g_return_val_if_fail (si != NULL, NULL);
	g_return_val_if_fail (not_available != NULL, NULL);
	return NULL;
}

void
ms_ole_summary_preview_destroy (MsOleSummaryPreview *d)
{
	g_return_if_fail (d != NULL);
}

MsOleSummaryPreview *
ms_ole_summary_get_preview (MsOleSummary *si, MsOleSummaryPID id,
			    gboolean *not_available)
{
	g_return_val_if_fail (si != NULL, NULL);
	g_return_val_if_fail (not_available != NULL, NULL);
	return NULL;
}

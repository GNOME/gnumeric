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
ms_ole_summary_open (MsOle *f)
{
	g_return_val_if_fail (f != NULL, NULL);
	return NULL;
}

/*
 * Opens 's' as SummaryInformation, returns NULL on failure
 */
MsOleSummary *
ms_ole_summary_open_stream (MsOleStream *s)
{
	g_return_val_if_fail (s != NULL, NULL);
	return NULL;
}

void ms_ole_summary_destroy (MsOleSummary *si)
{
	g_return_if_fail (si != NULL);
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

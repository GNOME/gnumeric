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

#ifndef MS_OLE_SUMMARY_H
#define MS_OLE_SUMMARY_H

typedef struct {
	guint8       class_id[16];
	GArray       *sections;
	GArray       *items;
	MsOleStream *s;
} MsOleSummary;

/*
 * Opens 'SummaryInformation', returns NULL on failure
 */
MsOleSummary *ms_ole_summary_open           (MsOle *f);

/*
 * Opens 's' as SummaryInformation, returns NULL on failure
 */
MsOleSummary *ms_ole_summary_open_stream    (MsOleStream *s);

void          ms_ole_summary_destroy        (MsOleSummary *si);


/*
 * Can be used to interrogate a summary item as to its type
 */
typedef enum {
	MS_OLE_SUMMARY_TYPE_STRING = 0x10,
	MS_OLE_SUMMARY_TYPE_TIME   = 0x20,
	MS_OLE_SUMMARY_TYPE_LONG   = 0x30,
	MS_OLE_SUMMARY_TYPE_OTHER  = 0x40,
} MsOleSummaryType;
#define MS_OLE_SUMMARY_TYPE(x) ((MsOleSummaryType)((x)>>8))

/*
 *  The MS byte specifies the type, the LS byte is the
 * 'standard' MS PID.
 */
typedef enum {
/* String properties */
	MS_OLE_SUMMARY_TITLE          = 0x1002,
	MS_OLE_SUMMARY_SUBJECT        = 0x1003,
	MS_OLE_SUMMARY_AUTHOR         = 0x1004,
	MS_OLE_SUMMARY_KEYWORDS       = 0x1005,
	MS_OLE_SUMMARY_COMMENTS       = 0x1006,
	MS_OLE_SUMMARY_TEMPLATE       = 0x1007,
	MS_OLE_SUMMARY_LASTAUTHOR     = 0x1008,
	MS_OLE_SUMMARY_REVNUMBER      = 0x1009,
	MS_OLE_SUMMARY_APPNAME        = 0x1012,
	
/* Time properties */
	MS_OLE_SUMMARY_TOTAL_EDITTIME = 0x200A,
	MS_OLE_SUMMARY_LASTPRINTED    = 0x200B,
	MS_OLE_SUMMARY_CREATED        = 0x200C,
	MS_OLE_SUMMARY_LASTSAVED      = 0x200D,
	
/* Long integer properties */
	MS_OLE_SUMMARY_PAGECOUNT      = 0x300E,
	MS_OLE_SUMMARY_WORDCOUNT      = 0x300F,
	MS_OLE_SUMMARY_CHARCOUNT      = 0x3010,
	MS_OLE_SUMMARY_SECURITY       = 0x3013,

/* Security */	
	MS_OLE_SUMMARY_THUMBNAIL      = 0x4011
} MsOleSummaryPID;

/* bit masks for security long integer */
#define MsOleSummaryAllSecurityFlagsEqNone        0x00
#define MsOleSummarySecurityPassworded            0x01
#define MsOleSummarySecurityRORecommended         0x02
#define MsOleSummarySecurityRO                    0x04
#define MsOleSummarySecurityLockedForAnnotations  0x08

typedef struct {
	GTimeVal time;
	GDate    date;
} MsOleSummaryTime;

typedef struct {
	guint32 len;
	guint8 *data;
} MsOleSummaryPreview;
void                ms_ole_summary_preview_destroy (MsOleSummaryPreview d);

/* Ensure that you destroy / free returned values after use */
char                *ms_ole_summary_get_string  (MsOleSummary *si, MsOleSummaryPID id,
						 gboolean *available);

guint32              ms_ole_summary_get_long    (MsOleSummary *si, MsOleSummaryPID id,
						 gboolean *available);

MsOleSummaryTime     ms_ole_summary_get_time    (MsOleSummary *si, MsOleSummaryPID id,
						 gboolean *available);

MsOleSummaryPreview  ms_ole_summary_get_preview (MsOleSummary *si, MsOleSummaryPID id,
						 gboolean *available);

#endif

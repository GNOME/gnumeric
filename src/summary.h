/*
 * summary.h:  Summary Information management
 *
 * Author:
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 1999 Michael Meeks
 */

#ifndef GNUMERIC_SUMMARY_H
#define GNUMERIC_SUMMARY_H

typedef struct _SummaryItem SummaryItem;
typedef struct _SummaryInfo SummaryInfo;

typedef enum { SUMMARY_STRING,
               SUMMARY_BOOLEAN,
	       SUMMARY_SHORT,
	       SUMMARY_INT,
	       SUMMARY_TIME
	     } SummaryItemType;

/* See dialog-summary.c before fiddling */
typedef enum { SUMMARY_I_CODEPAGE,
               SUMMARY_I_TITLE,
	       SUMMARY_I_SUBJECT,
	       SUMMARY_I_AUTHOR,
	       SUMMARY_I_KEYWORDS,
	       SUMMARY_I_COMMENTS,
	       SUMMARY_I_TEMPLATE,
	       SUMMARY_I_LASTAUTHOR,
	       SUMMARY_I_REVNUMBER,
	       SUMMARY_I_LASTPRINTED,
	       SUMMARY_I_CREATED,
	       SUMMARY_I_LASTSAVED,
	       SUMMARY_I_PAGECOUNT,
	       SUMMARY_I_WORDCOUNT,
	       SUMMARY_I_CHARCOUNT,
	       SUMMARY_I_APP,
	       SUMMARY_I_SECURITY,

	       SUMMARY_I_CATEGORY,
	       SUMMARY_I_PRESFORMAT,
	       SUMMARY_I_BYTECOUNT,
	       SUMMARY_I_LINECOUNT,
	       SUMMARY_I_PARCOUNT ,
	       SUMMARY_I_SLIDECOUNT,
	       SUMMARY_I_NOTECOUNT,
	       SUMMARY_I_HIDDENCOUNT,
	       SUMMARY_I_MMCLIPCOUNT,
	       SUMMARY_I_SCALE,
	       SUMMARY_I_MANAGER,
	       SUMMARY_I_COMPANY,
	       SUMMARY_I_LINKSDIRTY,

	       SUMMARY_I_MAX } SummaryItemBuiltin;

/* Builtin names: use summary_item_name[SUMMARY_I_TITLE] */
extern const gchar *summary_item_name[SUMMARY_I_MAX];

/*
 *  Each Summary Item has a name it is hashed on,
 * and a value.
 */
struct _SummaryItem {
	SummaryItemType   type;
	gchar             *name;
	union {
		gchar    *txt;
		GTimeVal  time;
		gint      i;
		gshort	  short_i;
		gboolean  boolean;
	} v;
};

SummaryItem *summary_item_new_boolean (gchar const *name, gboolean i);
SummaryItem *summary_item_new_short   (gchar const *name, gshort i);
SummaryItem *summary_item_new_int     (gchar const *name, gint i);
SummaryItem *summary_item_new_time    (gchar const *name, GTimeVal t);
SummaryItem *summary_item_new_string  (gchar const *name, gchar const *string, gboolean copy);
char        *summary_item_as_text     (SummaryItem const *sit);
void         summary_item_free        (SummaryItem *sit);
SummaryItem *summary_item_copy        (SummaryItem const *sit);
SummaryItem *summary_item_by_name     (gchar const *name, SummaryInfo const *sin);
char        *summary_item_as_text_by_name (gchar const *name, SummaryInfo const *sin);

struct _SummaryInfo {
	GHashTable *names;
	gboolean modified;
};

SummaryInfo *summary_info_new	  (void);
GList       *summary_info_as_list (SummaryInfo const *sin);
gboolean     summary_info_add     (SummaryInfo *sin, SummaryItem *sit);
void         summary_info_default (SummaryInfo *sin);
void         summary_info_dump    (SummaryInfo *sin);
void         summary_info_free    (SummaryInfo *sin);

#endif

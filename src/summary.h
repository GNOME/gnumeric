/*
 * summary.h:  Summary Information management
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 *
 * (C) 1999 Michael Meeks
 */

typedef struct _SummaryItem SummaryItem;
typedef struct _SummaryInfo SummaryInfo;

typedef enum { SUMMARY_STRING, SUMMARY_INT, SUMMARY_TIME } SummaryItemType;

typedef enum { SUMMARY_I_TITLE,
	       SUMMARY_I_SUBJECT,
	       SUMMARY_I_AUTHOR,
	       SUMMARY_I_KEYWORDS,
	       SUMMARY_I_COMMENTS,
	       SUMMARY_I_APP,
	       SUMMARY_I_MAX } SummaryItemBuiltin;

/* Builtin names: use summary_item_name[SUMMARY_I_TITLE] */
gchar *summary_item_name[SUMMARY_I_MAX];

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
	} v;
};

SummaryItem *summary_item_new_int    (const gchar *name, gint i);
SummaryItem *summary_item_new_time   (const gchar *name, GTimeVal t);
SummaryItem *summary_item_new_string (const gchar *name, const gchar *string);
void         summary_item_free       (SummaryItem *sit);

struct _SummaryInfo {
	GHashTable *names;
};

SummaryInfo *summary_info_new     (void);
SummaryItem *summary_info_get     (SummaryInfo *sin, char *name);
void         summary_info_add     (SummaryInfo *sin, SummaryItem *sit);
void         summary_info_default (SummaryInfo *sin);
void         summary_info_dump    (SummaryInfo *sin);
void         summary_info_free    (SummaryInfo *sin);

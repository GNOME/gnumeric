#ifndef GNUMERIC_FORMAT_TEMPLATE_H
#define GNUMERIC_FORMAT_TEMPLATE_H

#include "gnumeric.h"

/*
 * FORMAT TEMPLATE RULES
 *
 * Authors :
 *   Almer S. Tigelaar <almer1@dds.nl>
 *   Jody Goldberg <jody@gnome.org>
 *
 * ----------------------------------------------------------------------------
 * Placement :
 *   offset :
 *     1. These can only be positive and indicate the number of
 *        columns from the side (see gravity)
 *   offset_gravity :
 *     1. This is the side to count offset from,
 *        gravity (for rows : +1=top, -1=bottom; for cols : +1=left, -1=right).
 *
 * Dimensions :
 *   size :
 *     1. The number of cols/rows from the offset, if this is <=0 than this is
 *        relative to the 'far side' otherwise it is relative to the offset.
 *
 * Frequency :
 *   direction :
 *     1. The direction to repeat in, this can be either horizontal
 *        or vertical.
 *   repeat :
 *     1. When repeat >= 0, we will repeat
 *        repeat times, if it is something else (preferably -1) we
 *        simply repeat _indefinitely_ in vertical or horizontal directions.
 *   skip :
 *     1. When skip is 0 or smaller than 0 we don't skip
 *        any rows or cols in between applications.
 *   edge :
 *     1. Can be 0 or greater. Specifies the number of rows to at least
 *        not but a repetetive style calculated from the far side.
 * ----------------
 */
typedef struct {
	int offset;        	/* Offset (top/left) */
	int offset_gravity; 	/* Gravity +1 means relative to top/left, -1 relative to bottom/right */
	int size;		/* Dimensions (These form the bottom right coordinates) */
} FormatColRowInfo;

/* WARNING : do not change these or persistence will break */
typedef enum {
	FREQ_DIRECTION_NONE,
	FREQ_DIRECTION_HORIZONTAL,
	FREQ_DIRECTION_VERTICAL
} FreqDirection;

typedef struct {
	GList *categories;
	gchar *orig_name, *name;
	gchar *description;
	gint lang_score;
} FormatTemplateCategoryGroup;

typedef struct {
	char *directory;
	char *orig_name, *name;
	char *description;
	int lang_score;
	gboolean is_writable;
} FormatTemplateCategory;

struct _FormatTemplate {
	FormatTemplateCategory *category;
	GSList *members;	/* the actual TemplateMembers */
	char *filename;
	char *author;
	char *name;
	char *description;

	/* what to enable */
	gboolean number : 1;
	gboolean border : 1;
	gboolean font : 1;
	gboolean patterns : 1;
	gboolean alignment : 1;

/* <private> */
	/* pre-calculate styles */
	GHashTable *table;
	gboolean invalidate_hash;

	Range dimension;
};

typedef struct {
	FormatColRowInfo row; /* Row info */
	FormatColRowInfo col; /* Col info */

	/* Frequency (How many times to repeat) and in which
	 * direction and when to stop.
	 */
	FreqDirection direction;
	int repeat;
	int skip;
	int edge;

	MStyle *mstyle;       /* Style to apply */
} TemplateMember;

/*
 * Functions for FormatTemplate
 */
void            format_template_free           (FormatTemplate *ft);
FormatTemplate *format_template_clone          (FormatTemplate *ft);
FormatTemplate *format_template_new            (void);
FormatTemplate *format_template_new_from_file  (char const *filename,
						CommandContext *context);
gboolean        format_template_save           (FormatTemplate const *ft,
						CommandContext *cc);

gint                  format_template_compare_name             (gconstpointer a, gconstpointer b);

void                  format_template_attach_member            (FormatTemplate *ft, TemplateMember *member);
void                  format_template_detach_member            (FormatTemplate *ft, TemplateMember *member);
MStyle               *format_template_get_style                (FormatTemplate *ft, int row, int col);
void                  format_template_apply_to_sheet_regions   (FormatTemplate *ft, Sheet *sheet, GSList *regions);
gboolean	      format_template_check_valid	       (FormatTemplate *ft, GSList *regions,
								CommandContext *cc);

void                  format_template_set_name                 (FormatTemplate *ft, char const *name);
void                  format_template_set_author               (FormatTemplate *ft, char const *author);
void                  format_template_set_description          (FormatTemplate *ft, char const *description);

TemplateMember *format_template_member_new (void);
TemplateMember *format_template_member_clone (TemplateMember *member);
void format_template_member_free (TemplateMember *member);

#endif /* GNUMERIC_FORMAT_TEMPLATE_H */

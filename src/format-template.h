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
 *
 */
typedef struct {
	/*
	 * Placement (These form the top, left coordinates)
	 */
	int offset;          /* Offset (top/left) */
	int offset_gravity;  /* Gravity +1 means relative to top/left, -1 relative to bottom/right */

        /*
	 * Dimensions (These form the bottom right coordinates)
	 */
	int size;
} FormatColRowInfo;

typedef enum {
	FREQ_DIRECTION_NONE,
	FREQ_DIRECTION_HORIZONTAL,
	FREQ_DIRECTION_VERTICAL
} FreqDirection;

typedef struct {
	FormatColRowInfo row; /* Row info */
	FormatColRowInfo col; /* Col info */

	/*
	 * Frequency (How many times to repeat) and in which
	 * direction and when to stop.
	 */
	FreqDirection direction;
	int repeat;
	int skip;
	int edge;

	MStyle *mstyle;       /* Style to apply */
} TemplateMember;

typedef struct _FormatTemplateCategory FormatTemplateCategory;
typedef struct _FormatTemplateCategoryGroup FormatTemplateCategoryGroup;

typedef struct {
        /* The filename of this template */
	GString *filename;

	/*
	 * Some usual information
	 */
	GString *author;
	GString *name;
	GString *description;

	/*
	 * The most important thing the actual TemplateMembers are
	 * stored in this singly-linked-list.
	 */
	GSList *members;

	/*
	 * Command context for some error reporting
	 */
	CommandContext *context;

	/*
	 * What to filter
	 */
	gboolean number, border, font, patterns, alignment;

	/*
	 * The hashtable used to pre-calculate styles
	 */
	GHashTable *table;
	gboolean invalidate_hash;

	Range dimension;

	/*
	 * Category it came from.
	 */
	FormatTemplateCategory *category;
} FormatTemplate;

struct _FormatTemplateCategory {
	gchar *directory;
	gchar *orig_name, *name;
	gchar *description;
	gint lang_score;
	gboolean is_writable;
};

struct _FormatTemplateCategoryGroup {
	GList *categories;
	gchar *orig_name, *name;
	gchar *description;
	gint lang_score;
};

/*
 * Functions for FormatColRowInfo
 */
FormatColRowInfo      format_col_row_info_make (int offset, int offset_gravity,
						int size);

/*
 * Functions for TemplateMember
 */
TemplateMember       *format_template_member_new          (void);
TemplateMember       *format_template_member_clone        (TemplateMember *member);
void                  format_template_member_free         (TemplateMember *member);

FormatColRowInfo      format_template_member_get_row_info  (TemplateMember *member);
FormatColRowInfo      format_template_member_get_col_info  (TemplateMember *member);
FreqDirection         format_template_member_get_direction (TemplateMember *member);
int                   format_template_member_get_repeat    (TemplateMember *member);
int                   format_template_member_get_skip      (TemplateMember *member);
int                   format_template_member_get_edge      (TemplateMember *member);
MStyle               *format_template_member_get_style     (TemplateMember *member);

void                  format_template_member_set_row_info  (TemplateMember *member, FormatColRowInfo row_info);
void                  format_template_member_set_col_info  (TemplateMember *member, FormatColRowInfo col_info);
void                  format_template_member_set_direction (TemplateMember *member, FreqDirection direction);
void                  format_template_member_set_repeat    (TemplateMember *member, int repeat);
void                  format_template_member_set_skip      (TemplateMember *member, int skip);
void                  format_template_member_set_edge      (TemplateMember *member, int edge);
void                  format_template_member_set_style     (TemplateMember *member, MStyle *mstyle);

/*
 * Functions for FormatTemplate
 */
FormatTemplate       *format_template_new                      (CommandContext *context);
FormatTemplate       *format_template_clone                    (FormatTemplate *ft);
void                  format_template_free                     (FormatTemplate *ft);
FormatTemplate       *format_template_new_from_file            (CommandContext *context, const char *filename);
int                   format_template_save                     (FormatTemplate *ft);

gint                  format_template_compare_name             (gconstpointer a, gconstpointer b);

void                  format_template_attach_member            (FormatTemplate *ft, TemplateMember *member);
void                  format_template_detach_member            (FormatTemplate *ft, TemplateMember *member);
MStyle               *format_template_get_style                (FormatTemplate *ft, int row, int col);
void                  format_template_apply_to_sheet_regions   (FormatTemplate *ft, Sheet *sheet, GSList *regions);
gboolean	      format_template_check_valid	       (FormatTemplate *ft, GSList *regions);

char                 *format_template_get_filename             (FormatTemplate *ft);
char                 *format_template_get_name                 (FormatTemplate *ft);
char                 *format_template_get_author               (FormatTemplate *ft);
char                 *format_template_get_description          (FormatTemplate *ft);
GSList               *format_template_get_members              (FormatTemplate *ft);
FormatTemplateCategory *format_template_get_category           (FormatTemplate *ft);

void                  format_template_set_filename             (FormatTemplate *ft, const char *filename);
void                  format_template_set_name                 (FormatTemplate *ft, const char *name);
void                  format_template_set_author               (FormatTemplate *ft, const char *author);
void                  format_template_set_description          (FormatTemplate *ft, const char *description);
void                  format_template_set_category             (FormatTemplate *ft, FormatTemplateCategory *category);

void                  format_template_set_filter               (FormatTemplate *ft,
							        gboolean number, gboolean border,
							        gboolean font, gboolean patterns,
							        gboolean alignment);
void                  format_template_set_size                 (FormatTemplate *ft,
								int x1, int y1, int x2, int y2);
#endif /* GNUMERIC_FORMAT_TEMPLATE_H */

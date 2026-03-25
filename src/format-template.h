#ifndef GNM_FORMAT_TEMPLATE_H_
#define GNM_FORMAT_TEMPLATE_H_

#include <gnumeric.h>

G_BEGIN_DECLS

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
	int offset;       	/* Offset (top/left) */
	int offset_gravity;	/* Gravity +1 means relative to top/left, -1 relative to bottom/right */
	int size;		/* Dimensions (These form the bottom right coordinates) */
} GnmFTColRowInfo;

/* WARNING : do not change these or persistence will break */
typedef enum {
	FREQ_DIRECTION_NONE,
	FREQ_DIRECTION_HORIZONTAL,
	FREQ_DIRECTION_VERTICAL
} GnmFTFreqDirection;

#define GNM_TYPE_FT_CATEGORY_GROUP (gnm_ft_category_group_get_type ())
G_DECLARE_FINAL_TYPE (GnmFTCategoryGroup, gnm_ft_category_group, GNM, FT_CATEGORY_GROUP, GObject)

struct _GnmFTCategoryGroup {
	GObject parent;

	/* A collection of categories of the same name from different paths */
	GList *categories;

	/* translatable via gettext in the std message domain */
	char *name;
	char *description;
};

#define GNM_TYPE_FT_CATEGORY (gnm_ft_category_get_type ())
G_DECLARE_FINAL_TYPE (GnmFTCategory, gnm_ft_category, GNM, FT_CATEGORY, GObject)

struct _GnmFTCategory {
	GObject parent;
	char *directory;

	/* translatable via gettext in the std message domain */
	char *name;
	char *description;
};

GType gnm_ft_get_type (void);
#define GNM_TYPE_FT (gnm_ft_get_type ())
#define GNM_FT(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_FT_TYPE, GnmFT))
#define GNM_IS_FT(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_FT_TYPE))

struct GnmFT_ {
	GObject parent;
	GnmFTCategory *category;
	GSList *members;	/* the actual TemplateMembers */
	char *filename;
	char *author;
	/* translatable via gettext in the std message domain */
	char *name;
	char *description;

	/* what to enable */
	gboolean number;
	gboolean border;
	gboolean font;
	gboolean patterns;
	gboolean alignment;

	struct _FormatEdges {
		gboolean left;
		gboolean right;
		gboolean top;
		gboolean bottom;
	} edges;

/* <private> */
	/* pre-calculate styles */
	GHashTable *table;
	gboolean invalidate_hash;

	GnmRange dimension;
};

#define GNM_TYPE_FT_MEMBER (gnm_ft_member_get_type ())
G_DECLARE_FINAL_TYPE (GnmFTMember, gnm_ft_member, GNM, FT_MEMBER, GObject)

struct _GnmFTMember {
	GObject parent;
	GnmFTColRowInfo row; /* Row info */
	GnmFTColRowInfo col; /* Col info */

	/* Frequency (How many times to repeat) and in which
	 * direction and when to stop.
	 */
	GnmFTFreqDirection direction;
	int repeat;
	int skip;
	int edge;

	GnmStyle *mstyle;       /* Style to apply */
};

/*
 * Functions for GnmFT
 */
GnmFT		*gnm_ft_clone          (GnmFT const *ft);
GnmFT 		*gnm_ft_new_from_file  (char const *filename,
					GOCmdContext *context);

gint		 gnm_ft_compare_name   (gconstpointer a, gconstpointer b);

GnmStyle	*gnm_ft_get_style      (GnmFT *ft, int row, int col);
void		 gnm_ft_apply_to_sheet_regions   (GnmFT *ft, Sheet *sheet, GSList *regions);
gboolean	 gnm_ft_check_valid   (GnmFT *ft, GSList *regions,
				       GOCmdContext *cc);

G_END_DECLS

#endif /* GNM_FORMAT_TEMPLATE_H_ */

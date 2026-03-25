#ifndef GNM_SORT_H_
#define GNM_SORT_H_

#include <gnumeric.h>

G_BEGIN_DECLS

GType gnm_sort_data_get_type (void);
#define GNM_SORT_DATA_TYPE (gnm_sort_data_get_type ())
#define GNM_SORT_DATA(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_SORT_DATA_TYPE, GnmSortData))
#define GNM_IS_SORT_DATA(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_SORT_DATA_TYPE))

typedef struct {
	int	 offset;
	gboolean asc;
	gboolean cs;
	gboolean val;
} GnmSortClause;

struct GnmSortData_ {
	GObject		parent;
	Sheet		*sheet;
	GnmRange	range;
	int		 num_clause;
	GnmSortClause	*clauses;
	gboolean	 top;
	gboolean	 retain_formats;
	char            *locale;
};

GnmSortData *gnm_sort_data_new    (void);
GnmSortData *gnm_sort_data_copy   (GnmSortData const *data);
void gnm_sort_position	     (GnmSortData *data, int *perm, GOCmdContext *cc);
int *gnm_sort_contents	     (GnmSortData *data, GOCmdContext *cc);

G_END_DECLS

#endif /* GNM_SORT_H_ */

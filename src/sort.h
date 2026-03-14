#ifndef GNM_SORT_H_
#define GNM_SORT_H_

#include <gnumeric.h>

G_BEGIN_DECLS

#define GNM_SORT_DATA_TYPE (gnm_sort_data_get_type ())
G_DECLARE_FINAL_TYPE (GnmSortData, gnm_sort_data, GNM, SORT_DATA, GObject)

typedef struct {
	int	 offset;
	gboolean asc;
	gboolean cs;
	gboolean val;
} GnmSortClause;

struct _GnmSortData {
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

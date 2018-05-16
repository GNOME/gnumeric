#ifndef _GNM_SORT_H_
# define _GNM_SORT_H_

#include <gnumeric.h>

G_BEGIN_DECLS

typedef struct {
	int	 offset;
	gboolean asc;
	gboolean cs;
	gboolean val;
} GnmSortClause;

struct _GnmSortData {
	Sheet		*sheet;
	GnmRange	*range;
	int		 num_clause;
	GnmSortClause	*clauses;
	gboolean	 top;
	gboolean	 retain_formats;
	char            *locale;
};

GType gnm_sort_data_get_type (void);
void gnm_sort_data_destroy   (GnmSortData *data);
GnmSortData *gnm_sort_data_copy   (GnmSortData *data);
void gnm_sort_position	     (GnmSortData *data, int *perm, GOCmdContext *cc);
int *gnm_sort_contents	     (GnmSortData *data, GOCmdContext *cc);
int *gnm_sort_permute_invert (int const *perm, int length);

G_END_DECLS

#endif /* _GNM_SORT_H_ */

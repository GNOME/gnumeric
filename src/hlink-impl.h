#ifndef _GNM_HLINK_IMPL_H_
# define _GNM_HLINK_IMPL_H_

#include "hlink.h"

G_BEGIN_DECLS

struct _GnmHLink {
	GObject obj;
	gchar *tip;
	gchar *target;
	Sheet *sheet;
};

typedef struct {
	GObjectClass obj;

	gboolean (*Activate) (GnmHLink *link, WBCGtk *wbcg);
	void (*set_sheet) (GnmHLink *link, Sheet *sheet);
	void (*set_target) (GnmHLink *link, const char *target);
	const char * (*get_target) (GnmHLink const *link);
} GnmHLinkClass;

G_END_DECLS

#endif /* _GNM_HLINK_IMPL_H_ */

/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_HLINK_IMPL_H_
# define _GNM_HLINK_IMPL_H_

#include "hlink.h"

G_BEGIN_DECLS

struct _GnmHLink {
	GObject obj;
	gchar *tip;
	gchar *target;
};

typedef struct {
	GObjectClass obj;

	gboolean (*Activate) (GnmHLink *link, WorkbookControl *wbc);
} GnmHLinkClass;

G_END_DECLS

#endif /* _GNM_HLINK_IMPL_H_ */

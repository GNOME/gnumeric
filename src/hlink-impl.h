/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef GNUMERIC_HLINK_IMPL_H
#define GNUMERIC_HLINK_IMPL_H

#include "hlink.h"

struct _GnmHLink {
	GObject obj;
	guchar *tip;
	guchar *target;
};

typedef struct {
	GObjectClass obj;

	gboolean (*Activate) (GnmHLink *link, WorkbookControl *wbc);
} GnmHLinkClass;

#endif /* GNUMERIC_HLINK_IMPL_H */

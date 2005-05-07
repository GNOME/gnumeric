/* vim: set sw=8 ts=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef GNM_MS_OFFICE_ESCHER_H
#define GNM_MS_OFFICE_ESCHER_H

/**
 * ms-escher.h: MS Office drawing layer support
 *
 * Authors:
 *    Jody Goldberg (jody@gnome.org)
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 1998-2001 Michael Meeks
 * (C) 2002-2005 Jody Goldberg
 **/
#include "ms-excel-read.h"
#include "ms-container.h"
#include "ms-obj.h"

struct _MSEscherBlip {
	char const   *type;
	guint8       *data;
	guint32	      data_len;
	gboolean      needs_free;
};

MSObjAttrBag *ms_escher_parse (BiffQuery  *q, MSContainer *container,
			       gboolean return_attrs);

void ms_escher_blip_free (MSEscherBlip *blip);

#if 0
typedef struct _MSEscherWriter MSEscherWriter;
MSEscherWriter *ms_escher_writer_new (BiffPut *bp);
void		ms_escher_writer_commit (MSEscherWriter *ew);
#endif

#endif /* GNM_MS_OFFICE_ESCHER_H */

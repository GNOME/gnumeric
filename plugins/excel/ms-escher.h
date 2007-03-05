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

/******************************************************/

typedef struct _MSEscherSp MSEscherSp;
MSEscherSp *ms_escher_sp_new        (void);
void	    ms_escher_sp_free	    (MSEscherSp *sp);
guint32	    ms_escher_sp_len        (MSEscherSp const *sp);
void	    ms_escher_sp_add_OPT    (MSEscherSp *sp, guint16 id, guint32 val,
				     gpointer complex_val);
void	    ms_escher_sp_set_anchor (MSEscherSp *sp,
				     SheetObjectAnchor const *anchor,
				     guint16 anchor_flags);
void	    ms_escher_sp_write      (MSEscherSp *sp, BiffPut *bp,
				     guint16 shape, guint32 spid);

#endif /* GNM_MS_OFFICE_ESCHER_H */

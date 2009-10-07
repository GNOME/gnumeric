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
#include <sheet-object.h>

enum {
	MSEP_FILLCOLOR = 0x0181,
	MSEP_LINECOLOR = 0x01c0,
	MSEP_LINEWIDTH = 0x01cb
};



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

guint ms_escher_get_inst (GString *buf, gsize marker);
void ms_escher_set_inst (GString *buf, gsize marker, guint inst);

gsize ms_escher_spcontainer_start (GString *buf);
void ms_escher_spcontainer_end (GString *buf, gsize marker);

void ms_escher_sp (GString *buf, guint32 spid, guint16 shape, guint32 flags);

gsize ms_escher_opt_start (GString *buf);
void ms_escher_opt_add_simple (GString *buf, gsize marker,
			       guint16 pid, gint32 val);
void ms_escher_opt_add_color (GString *buf, gsize marker,
			      guint16 pid, GOColor c);
void ms_escher_opt_add_str_wchar (GString *buf, gsize marker, GString *extra,
				  guint16 pid, const char *str);
void ms_escher_opt_end (GString *buf, gsize marker);

void ms_escher_clientanchor (GString *buf, SheetObjectAnchor const *anchor);

void ms_escher_clientdata (GString *buf, gpointer data, gsize len);

/******************************************************/

#endif /* GNM_MS_OFFICE_ESCHER_H */

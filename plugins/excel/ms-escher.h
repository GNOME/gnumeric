#ifndef GNUMERIC_MS_OFFICE_ESCHER_H
#define GNUMERIC_MS_OFFICE_ESCHER_H

/**
 * ms-escher.h: MS Office drawing layer support
 *
 * Author:
 *    Jody Goldberg (jody@gnome.org)
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 1998-2002 Jody Goldberg, Michael Meeks
 **/
#include "ms-excel-read.h"
#include "ms-container.h"

struct _MSEscherBlip
{
	char const   *type;
	guint8       *data;
	guint32	      data_len;
	gboolean      needs_free;
};

void ms_escher_parse     (BiffQuery  *q, MSContainer *container);
void ms_escher_blip_free (MSEscherBlip *blip);

#endif /* GNUMERIC_MS_OFFICE_ESCHER_H */

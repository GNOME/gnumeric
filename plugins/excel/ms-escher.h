#ifndef GNUMERIC_MS_OFFICE_ESCHER_H
#define GNUMERIC_MS_OFFICE_ESCHER_H

/**
 * ms-escher.h: MS Office drawing layer support
 *
 * Author:
 *    Jody Goldberg (jody@gnome.org)
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 1998, 1999, 2000 Jody Goldberg, Michael Meeks
 **/
#include "ms-excel-read.h"
#include "ms-container.h"

#ifdef ENABLE_BONOBO
#include <bonobo.h>
#endif

struct _MSEscherBlip
{
	char const   *obj_id;
#ifdef ENABLE_BONOBO
	BonoboStream *stream;
#else
	guint8       *raw_data;
#endif
};

void ms_escher_parse        (BiffQuery  *q, MSContainer *container);
void ms_escher_blip_destroy (MSEscherBlip *blip);

#endif /* GNUMERIC_MS_OFFICE_ESCHER_H */

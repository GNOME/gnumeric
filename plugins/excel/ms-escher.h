#ifndef GNUMERIC_MS_OFFICE_ESCHER_H
#define GNUMERIC_MS_OFFICE_ESCHER_H

/**
 * ms-escher.h: MS Office drawing layer support
 *
 * Author:
 *    Jody Goldberg (jgoldberg@home.com)
 *    Michael Meeks (michael@imaginator.com)
 *
 * (C) 1998, 1999 Jody Goldberg, Michael Meeks
 **/
#include "ms-excel-read.h"

typedef struct {
	char const  * reproid;
#ifdef ENABLE_BONOBO
	GnomeStream * stream;
#else
	guint8      * raw_data;
#endif
} EscherBlip;

extern void ms_escher_parse (BiffQuery     *q,
			     ExcelWorkbook *wb,
			     ExcelSheet    *sheet);

extern void ms_escher_blip_destroy(EscherBlip * blip);

#endif /* GNUMERIC_MS_OFFICE_ESCHER_H */

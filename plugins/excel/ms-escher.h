#ifndef GNUMERIC_MS_OFFICE_ESCHER_H
#define GNUMERIC_MS_OFFICE_ESCHER_H

/**
 * ms-escher.h: MS Office drawing layer support
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 **/
#include "ms-excel-read.h"

extern void ms_escher_parse (BiffQuery     *q,
			     ExcelWorkbook *wb,
			     ExcelSheet    *sheet);

#endif /* GNUMERIC_MS_OFFICE_ESCHER_H */

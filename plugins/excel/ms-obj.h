#ifndef GNUMERIC_MS_OBJ_H
#define GNUMERIC_MS_OBJ_H

/**
 * ms-obj.h: MS Excel Graphic Object support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 **/

#include "ms-excel-read.h"

void ms_read_OBJ (BiffQuery *q, ExcelWorkbook * wb);
void ms_read_TXO (BiffQuery *q, ExcelWorkbook * wb);

#endif /* GNUMERIC_MS_OBJ_H */

#ifndef GNUMERIC_MS_OBJ_H
#define GNUMERIC_MS_OBJ_H

/**
 * ms-obj.h: MS Excel Graphic Object support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 **/

#include "ms-excel-read.h"

void ms_obj_read_obj (BiffQuery *q, ExcelWorkbook * wb);
void ms_obj_read_text (BiffQuery *q, ExcelWorkbook * wb, int const id);
void ms_obj_read_text_impl (BiffQuery *q, ExcelWorkbook * wb);

#endif /* GNUMERIC_MS_OBJ_H */

/**
 * ms-obj.h: MS Excel Graphic Object support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 **/

#ifndef GNUMERIC_MS_OBJ_H
#define GNUMERIC_MS_OBJ_H

void ms_obj_read_obj (BIFF_QUERY *q);
gboolean ms_chart_biff_read (MS_EXCEL_WORKBOOK *wb, BIFF_QUERY *q);

#endif

#ifndef GNUMERIC_SHEET_AUTOFILL_H
#define GNUMERIC_SHEET_AUTOFILL_H

#include "sheet.h"

typedef int (*AutofillFunction)     (Sheet *sheet,
				     int base_col, int base_row,
				     int w,        int h,
				     int end_col,  int end_row);

void    autofill_register_list      (const char *const *list);

void    sheet_autofill              (Sheet *sheet,
				     int base_col, int base_row,
				     int w,        int h,
				     int end_col,  int end_row);

#endif /* GNUMERIC_SHEET_AUTOFILL_H */

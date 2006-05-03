#ifndef GNUMERIC_SHEET_AUTOFILL_H
#define GNUMERIC_SHEET_AUTOFILL_H

#include "gnumeric.h"

void autofill_init  (void);
void autofill_shutdown  (void);

void sheet_autofill (Sheet *sheet, gboolean default_increment,
		     int base_col, int base_row,
		     int w,        int h,
		     int end_col,  int end_row);

char *sheet_autofill_hint (Sheet *sheet, gboolean default_increment,
			   int base_col, int base_row,
			   int w,        int h,
			   int end_col,  int end_row);

#endif /* GNUMERIC_SHEET_AUTOFILL_H */

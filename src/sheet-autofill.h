#ifndef SHEET_AUTOFILL
#define SHEET_AUTOFILL

void    autofill_init  (void);

typedef int (*AutofillFunction)     (Sheet *sheet,
				     int base_col, int base_row,
				     int w,        int h,
				     int end_col,  int end_row);

void    register_autofill_function  (AutofillFunction fn);

void    sheet_autofill              (Sheet *sheet,
				     int base_col, int base_row,
				     int w,        int h,
				     int end_col,  int end_row);

#endif

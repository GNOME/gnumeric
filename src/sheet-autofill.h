#ifndef SHEET_AUTOFILL
#define SHEET_AUTOFILL

typedef int (*AutofillFunction)     (Sheet *sheet,
				     int base_col, int base_row,
				     int w,        int h,
				     int end_col,  int end_row);

void    autofill_register_list      (char **list);

void    sheet_autofill              (Sheet *sheet,
				     int base_col, int base_row,
				     int w,        int h,
				     int end_col,  int end_row);

#endif

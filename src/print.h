#ifndef GNUMERIC_PRINT_H
#define GNUMERIC_PRINT_H

typedef enum {
	PRINT_ACTIVE_SHEET,
	PRINT_ALL_SHEETS,
	PRINT_SHEET_RANGE,
	PRINT_SHEET_SELECTION
} PrintRange;

void sheet_print (Sheet *sheet, gboolean preview,
		  PrintRange default_range);

#endif /* GNUMERIC_PRINT_H */

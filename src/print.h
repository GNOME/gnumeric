#ifndef GNUMERIC_PRINT_H
#define GNUMERIC_PRINT_H

#include "gnumeric.h"
#include "workbook-control-gui.h"

typedef enum {
	PRINT_ACTIVE_SHEET,
	PRINT_ALL_SHEETS,
	PRINT_SHEET_RANGE,
	PRINT_SHEET_SELECTION
} PrintRange;

void gnm_print_sheet (WorkbookControlGUI *wbcg, Sheet *sheet,
		      gboolean preview, PrintRange default_range);

#endif /* GNUMERIC_PRINT_H */

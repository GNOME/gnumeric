#ifndef GNUMERIC_PRINT_H
#define GNUMERIC_PRINT_H

#include "gnumeric.h"
#include <gsf/gsf-output.h>

#define GNUMERIC_PRINT_SETTING_PRINTRANGE_KEY		"GnumericPrintRange"
#define GNUMERIC_PRINT_SETTING_PRINT_FROM_SHEET_KEY	"GnumericPrintFromSheet"
#define GNUMERIC_PRINT_SETTING_PRINT_TO_SHEET_KEY	"GnumericPrintToSheet"

typedef enum {
	PRINT_ACTIVE_SHEET,
	PRINT_ALL_SHEETS,
	PRINT_SHEET_RANGE,
	PRINT_SHEET_SELECTION,
	PRINT_IGNORE_PRINTAREA,
	PRINT_SHEET_SELECTION_IGNORE_PRINTAREA
} PrintRange;

void gnm_print_sheet (WorkbookControl *wbc, Sheet *sheet,
		      gboolean preview, PrintRange default_range,
		      GsfOutput *export_dst);

/* Internal */
extern gboolean gnm_print_debug;

#endif /* GNUMERIC_PRINT_H */

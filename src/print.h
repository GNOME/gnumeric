/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_PRINT_H_
# define _GNM_PRINT_H_

#include "gnumeric.h"
#include <gsf/gsf-output.h>

G_BEGIN_DECLS

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

G_END_DECLS

#endif /* _GNM_PRINT_H_ */

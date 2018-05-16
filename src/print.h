#ifndef _GNM_PRINT_H_
# define _GNM_PRINT_H_

#include <gnumeric.h>
#include <gsf/gsf-output.h>
#include <cairo.h>

G_BEGIN_DECLS

#define GNUMERIC_PRINT_SETTING_PRINTRANGE_KEY		"GnumericPrintRange"
#define GNUMERIC_PRINT_SETTING_PRINT_FROM_SHEET_KEY	"GnumericPrintFromSheet"
#define GNUMERIC_PRINT_SETTING_PRINT_TO_SHEET_KEY	"GnumericPrintToSheet"
#define GNUMERIC_PRINT_SETTING_IGNORE_PAGE_BREAKS_KEY   "GnumericPrintIgnorePageBreaks"

GType gnm_print_range_get_type (void);
#define GNM_PRINT_RANGE_TYPE (gnm_print_range_get_type ())

typedef enum { /* These numbers are saved in pre 1.11.x .gnumeric files */
	/* In 1.11.x and later the names as defined in */
	/* gnm_print_range_get_type are used */
	GNM_PRINT_SAVED_INFO = -1,
	GNM_PRINT_ACTIVE_SHEET = 0,
	GNM_PRINT_ALL_SHEETS = 1,
	GNM_PRINT_ALL_SHEETS_INCLUDING_HIDDEN = 2,
	GNM_PRINT_SHEET_RANGE = 3,
	GNM_PRINT_SHEET_SELECTION = 4,
	GNM_PRINT_IGNORE_PRINTAREA = 5,
	GNM_PRINT_SHEET_SELECTION_IGNORE_PRINTAREA = 6
} PrintRange;

void gnm_print_sheet (WorkbookControl *wbc, Sheet *sheet,
		      gboolean preview, PrintRange default_range,
		      GsfOutput *export_dst);

void gnm_print_so (WorkbookControl *wbc, GPtrArray *sos,
		   GsfOutput *export_dst);

void gnm_print_sheet_objects (cairo_t *cr,
			      Sheet const *sheet,
			      GnmRange *range,
			      double base_x, double base_y);

G_END_DECLS

#endif /* _GNM_PRINT_H_ */

#ifndef GNUMERIC_DIALOG_STF_EXPORT_H
#define GNUMERIC_DIALOG_STF_EXPORT_H

#include "stf-export.h"
#include "workbook-control-gui.h"

/*
 * Returned result
 */
typedef struct {
	StfExportOptions_t *export_options;                /* Export Options */
} StfE_Result_t;

/*
 * MAIN Functions
 */
StfE_Result_t *stf_export_dialog             (WorkbookControlGUI *wbcg, Workbook *wb);
void           stf_export_dialog_result_free (StfE_Result_t *result);

#endif

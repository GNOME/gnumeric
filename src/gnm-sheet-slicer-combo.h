#ifndef _GNM_SHEET_SLICER_COMBO_H_
# define _GNM_SHEET_SLICER_COMBO_H_

#include <gnm-cell-combo.h>
#include <goffice-data.h>

G_BEGIN_DECLS

typedef struct {
	GnmCellCombo	parent;
#ifndef GOFFICE_NAMESPACE_DISABLE
	GODataSlicerField *dsf;
#endif
} GnmSheetSlicerCombo;

#define GNM_SHEET_SLICER_COMBO_TYPE     (gnm_sheet_slicer_combo_get_type ())
#define GNM_SHEET_SLICER_COMBO(obj)     (G_TYPE_CHECK_INSTANCE_CAST((obj), GNM_SHEET_SLICER_COMBO_TYPE, GnmSheetSlicerCombo))

GType gnm_sheet_slicer_combo_get_type (void);

G_END_DECLS

#endif /* _GNM_SHEET_SLICER_COMBO_H_ */

#ifndef _GNM_CELL_COMBO_H_
# define _GNM_CELL_COMBO_H_

#include <sheet-object-impl.h>

G_BEGIN_DECLS

typedef struct {
	SheetObject parent;

	SheetView	*sv;
} GnmCellCombo;

typedef struct {
	SheetObjectClass parent_class;
} GnmCellComboClass;

#define GNM_CELL_COMBO_TYPE     (gnm_cell_combo_get_type ())
#define GNM_CELL_COMBO(obj)     (G_TYPE_CHECK_INSTANCE_CAST((obj), GNM_CELL_COMBO_TYPE, GnmCellCombo))

GType gnm_cell_combo_get_type (void);

G_END_DECLS

#endif /* _GNM_CELL_COMBO_H_ */

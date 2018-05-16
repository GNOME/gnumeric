#ifndef _GNM_PREVIEW_GRID_IMPL_H_
#define _GNM_PREVIEW_GRID_IMPL_H_

#include <preview-grid.h>
#include <goffice/goffice.h>

G_BEGIN_DECLS

struct GnmPreviewGrid_ {
	GocGroup base;

	Sheet *sheet;

	struct GnmPreviewGridDefaults_ {
		int     col_width;
		int     row_height;
		GnmStyle *style;
		GnmValue  *value;
	} defaults;

	gboolean gridlines;
};

typedef struct {
	GocGroupClass parent_class;

	/* Virtuals */
	GnmStyle * (* get_cell_style) (GnmPreviewGrid *pg, int col, int row);
	GnmValue*(* get_cell_value) (GnmPreviewGrid *pg, int col, int row);
} GnmPreviewGridClass;

#define GNM_PREVIEW_GRID_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS((o), gnm_preview_grid_get_type (), GnmPreviewGridClass))

G_END_DECLS

#endif /* _GNM_PREVIEW_GRID_IMPL_H_ */

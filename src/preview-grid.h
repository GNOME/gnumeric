#ifndef _GNM_PREVIEW_GRID_H_
#define _GNM_PREVIEW_GRID_H_

#include <gnumeric.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define GNM_PREVIEW_GRID(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj), gnm_preview_grid_get_type (), GnmPreviewGrid))
#define GNM_IS_PREVIEW_GRID(o)  (G_TYPE_CHECK_INSTANCE_TYPE((o), gnm_preview_grid_get_type ()))

typedef struct GnmPreviewGrid_ GnmPreviewGrid;
GType gnm_preview_grid_get_type (void);

G_END_DECLS

#endif /* _GNM_PREVIEW_GRID_H_ */

#ifndef GNUMERIC_PREVIEW_GRID_CONTROLLER_H
#define GNUMERIC_PREVIEW_GRID_CONTROLLER_H

#include <gnome.h>
#include "gnumeric.h"
#include "preview-grid.h"
#include "cell.h"
#include "mstyle.h"

typedef int      (* PGridCtlGetRowHeight) (int row, gpointer data);
typedef int      (* PGridCtlGetColWidth)  (int col, gpointer data);

typedef Value *  (* PGridCtlGetCellContent) (int row, int col, gpointer data);
typedef MStyle * (* PGridCtlGetCellStyle)   (int row, int col, gpointer data);

typedef struct _PreviewGridController PreviewGridController;

PreviewGridController*     preview_grid_controller_new          (GnomeCanvas *canvas,
								 int rows, int cols,
								 int default_row_height, int default_col_width,
                                                                 PGridCtlGetRowHeight get_row_height_cb,
								 PGridCtlGetColWidth get_col_width_cb,
								 PGridCtlGetCellContent get_cell_content_cb,
								 PGridCtlGetCellStyle get_cell_style_cb,
								 gpointer cb_data, gboolean gridlines);
void                       preview_grid_controller_free         (PreviewGridController *controller);

void                       preview_grid_controller_force_redraw (PreviewGridController *controller);

#endif /* GNUMERIC_PREVIEW_GRID_CONTROLLER_H */

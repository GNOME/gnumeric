#ifndef GNUMERIC_PREVIEW_GRID_H
#define GNUMERIC_PREVIEW_GRID_H

#include "gnumeric.h"

#define PREVIEW_GRID(obj)          (GTK_CHECK_CAST((obj), preview_grid_get_type (), PreviewGrid))
#define IS_PREVIEW_GRID(o)         (GTK_CHECK_TYPE((o), preview_grid_get_type ()))

typedef int      (* PGridGetRowOffset) (int y, int* row_origin, gpointer data);
typedef int      (* PGridGetColOffset) (int x, int* col_origin, gpointer data);
typedef int      (* PGridGetColWidth)  (int col, gpointer data);
typedef int      (* PGridGetRowHeight) (int row, gpointer data);
typedef MStyle*  (* PGridGetCellStyle) (int row, int col, gpointer data);
typedef Value*   (* PGridGetCellValue) (int row, int col, gpointer data);

typedef struct _PreviewGrid PreviewGrid;
GtkType preview_grid_get_type (void);

#endif /* GNUMERIC_PREVIEW_GRID_H */


#ifndef GUI_GNUMERIC_H
#define GUI_GNUMERIC_H

#include "gnumeric.h"
#include <libgnomeui/libgnomeui.h>

typedef struct _ItemCursor		ItemCursor;
typedef struct _ItemGrid		ItemGrid;
typedef struct _ItemBar			ItemBar;
typedef struct _ItemEdit		ItemEdit;
typedef struct _GnumericCanvas		GnumericCanvas;
typedef struct _GnumericPane		GnumericPane;
typedef struct _SheetControlGUI		SheetControlGUI;
typedef struct _WorkbookControlGUI	WorkbookControlGUI;

typedef gboolean (*GnumericCanvasSlideHandler) (GnumericCanvas *gcanvas,
						int col, int row,
						gpointer user_data);
#endif /* GUI_GNUMERIC_H */

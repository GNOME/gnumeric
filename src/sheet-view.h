#ifndef GNUMERIC_SHEET_VIEW_H
#define GNUMERIC_SHEET_VIEW_H

#include "gnumeric.h"
#include <gtk/gtkobject.h>

struct _SheetView {
	GObject  base;

	Sheet	 	*sheet;
	WorkbookView	*wbv;
	GPtrArray	*controls;
};

typedef struct {
	GObjectClass   g_object_class;
} SheetViewClass;

#define SHEET_VIEW_TYPE     (sheet_view_get_type ())
#define SHEET_VIEW(obj)     (G_TYPE_CHECK_INSTANCE_CAST ((obj), SHEET_VIEW_TYPE, SheetView))
#define IS_SHEET_VIEW(o)    (G_TYPE_CHECK_INSTANCE_TYPE ((o), SHEET_VIEW_TYPE))
#define SHEET_VIEW_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), SHEET_VIEW_TYPE, SheetViewClass))

/* Lifecycle */
GType	   sheet_view_get_type	 (void);
SheetView *sheet_view_new	 (Sheet *sheet, WorkbookView *wbv);
void	   sv_attach_control (SheetView *sv, SheetControl *sc);
void	   sv_detach_control (SheetControl *sc);

/* Information */
Sheet		*sv_sheet	(SheetView const *sv);
WorkbookView	*sv_wbv		(SheetView const *sv);

/* Manipulation */

#define SHEET_VIEW_FOREACH_CONTROL(sv, control, code)				\
do {										\
	int j;									\
	GPtrArray *controls = sv->controls;					\
	if (controls != NULL) /* Reverse is important during destruction */	\
		for (j = controls->len; j-- > 0 ;) {				\
			SheetControl *control =					\
				g_ptr_array_index (controls, j);		\
			code							\
		}								\
} while (0)

#endif /* GNUMERIC_SHEET_VIEW_H */

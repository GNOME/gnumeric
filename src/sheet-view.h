#ifndef GNUMERIC_SHEET_VIEW_H
#define GNUMERIC_SHEET_VIEW_H

#include "gnumeric.h"
#include <gtk/gtkobject.h>

struct _SheetView {
	GtkObject  gtk_object;

	Sheet *s;
	GPtrArray *s_controls;
};

typedef struct {
	GtkObjectClass   gtk_object_class;
} SheetViewClass;

#define SHEET_VIEW_TYPE     (sheet_view_get_type ())
#define SHEET_VIEW(obj)     (GTK_CHECK_CAST ((obj), SHEET_VIEW_TYPE, SheetView))
#define IS_SHEET_VIEW(o)    (GTK_CHECK_TYPE ((o), SHEET_VIEW_TYPE))
#define SHEET_VIEW_CLASS(k) (GTK_CHECK_CLASS_CAST ((k), SHEET_VIEW_TYPE, SheetViewClass))

/* Lifecycle */
GtkType	   sheet_view_get_type	 (void);
SheetView *sheet_view_new	 (Sheet *sheet);
void	   sheet_view_init       (SheetView *sv, Sheet *sheet);
void	   s_view_attach_control (SheetView *sv, SheetControl *sc);
void	   s_view_detach_control (SheetControl *sc);

/* Information */
Sheet	*s_view_sheet (SheetView *sv);

#define SHEET_VIEW_FOREACH_CONTROL(sv, control, code)				\
do {										\
	int j;									\
	GPtrArray *s_controls = sv->s_controls;					\
	if (s_controls != NULL) /* Reverse is important during destruction */	\
		for (j = s_controls->len; j-- > 0 ;) {				\
			SheetControl *control =					\
				g_ptr_array_index (s_controls, j);		\
			code							\
		}								\
} while (0)

#endif /* GNUMERIC_SHEET_VIEW_H */

#ifndef GNUMERIC_SHEET_OBJECT_WIDGET_H
#define GNUMERIC_SHEET_OBJECT_WIDGET_H

#include "sheet-object.h"

/*
 * SheetObjectWidget:
 *
 * Widget for Bonobo objects
 */
#define SHEET_OBJECT_WIDGET_TYPE     (sheet_object_widget_get_type ())
#define SHEET_OBJECT_WIDGET(obj)     (GTK_CHECK_CAST((obj), SHEET_OBJECT_WIDGET_TYPE, SheetObjectWidget))
#define SHEET_OBJECT_WIDGET_CLASS(k) (GTK_CHECK_CLASS_CAST ((k), SHEET_OBJECT_WIDGET_TYPE))
#define IS_SHEET_WIDGET_OBJECT(o)    (GTK_CHECK_TYPE((o), SHEET_OBJECT_WIDGET_TYPE))

typedef struct _SheetObjectWidget SheetObjectWidget;

typedef GtkWidget *(*SheetWidgetRealizeFunction)(SheetObjectWidget *, SheetView *);

struct _SheetObjectWidget {
	SheetObject     parent_object;

	SheetWidgetRealizeFunction realize;
	void           *realize_closure;
};

typedef struct {
	SheetObjectClass parent_class;
} SheetObjectWidgetClass;

/*
 * Widget embedding object.
 *
 * Given that sheets must support multiple views, the user need to
 * create this widgets on demand (thus, the SheetWidgetRealizeFunction).
 *
 * It is the responsability of the user code to keep all the widgets
 * for the multiple views in sync.
 */
GtkType      sheet_object_widget_get_type  (void);
SheetObject *sheet_object_widget_new       (Sheet *sheet,
					    double x1, double y1,
					    double x2, double y2,
					    SheetWidgetRealizeFunction realize,
					    void *realize_closure);

void         sheet_object_widget_construct (SheetObjectWidget *sow,
					    Sheet *sheet,
					    double x1, double y1,
					    double x2, double y2,
					    SheetWidgetRealizeFunction realize,
					    void *realize_closure);
#endif /* GNUMERIC_SHEET_OBJECT_WIDGET_H */

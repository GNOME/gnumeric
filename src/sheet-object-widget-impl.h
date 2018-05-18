#ifndef _GNM_SHEET_OBJECT_WIDGET_IMPL_H_
# define _GNM_SHEET_OBJECT_WIDGET_IMPL_H_

#include <sheet-object-widget.h>
#include <sheet-object-impl.h>

G_BEGIN_DECLS

typedef struct {
	SheetObjectView parent;
} SOWidgetView;

typedef struct {
	SheetObjectViewClass parent_class;
} SOWidgetViewClass;

G_END_DECLS

#endif /* _GNM_SHEET_OBJECT_WIDGET_H_ */

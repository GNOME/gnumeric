#ifndef GNUMERIC_SHEET_OBJECT_WIDGET_H
#define GNUMERIC_SHEET_OBJECT_WIDGET_H

#include "sheet-object.h"

void sheet_object_widget_register (void);

GType sheet_widget_label_get_type	(void);
GType sheet_widget_frame_get_type	(void);
GType sheet_widget_button_get_type	(void);
GType sheet_widget_scrollbar_get_type	(void);
GType sheet_widget_checkbox_get_type	(void);
GType sheet_widget_radio_button_get_type(void);
GType sheet_widget_list_get_type	(void);
GType sheet_widget_combo_get_type	(void);

void sheet_widget_scrollbar_set_details	(SheetObject *so, GnmExpr const *link,
					 int value, int min, int max, int inc, int page);
void sheet_widget_checkbox_set_link	(SheetObject *so, GnmExpr const *expr);

#endif /* GNUMERIC_SHEET_OBJECT_WIDGET_H */

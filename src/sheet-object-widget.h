#ifndef GNUMERIC_SHEET_OBJECT_WIDGET_H
#define GNUMERIC_SHEET_OBJECT_WIDGET_H

#include "sheet-object.h"
#include <pango/pango-attributes.h>

void sheet_object_widget_register (void);

GType sheet_widget_frame_get_type	 (void); /* convert to non-widget item */
GType sheet_widget_button_get_type	 (void);
GType sheet_widget_checkbox_get_type	 (void);
GType sheet_widget_toggle_button_get_type(void);
GType sheet_widget_radio_button_get_type (void);

/*Descendents of the list_base sheet widget object*/
GType sheet_widget_list_get_type	 (void);
GType sheet_widget_combo_get_type	 (void);

/*Descendents of the adjustment sheet widget object*/
GType sheet_widget_scrollbar_get_type	 (void);
GType sheet_widget_slider_get_type       (void);
GType sheet_widget_spinbutton_get_type   (void);

void sheet_widget_adjustment_set_details (SheetObject *so, GnmExpr const *link,
					  int value, int min, int max, int inc, int page);
void sheet_widget_checkbox_set_link	 (SheetObject *so, GnmExpr const *expr);
void sheet_widget_checkbox_set_label	 (SheetObject *so, char const *str);
void sheet_widget_button_set_label	 (SheetObject *so, char const *str);
void sheet_widget_radio_button_set_label (SheetObject *so, char const *str);

void sheet_widget_button_set_markup      (SheetObject *so, PangoAttrList *markup);

#endif /* GNUMERIC_SHEET_OBJECT_WIDGET_H */

#ifndef GNUMERIC_SHEET_OBJECT_WIDGET_H
#define GNUMERIC_SHEET_OBJECT_WIDGET_H

#include "sheet-object.h"

void sheet_object_widget_register (void);

void sheet_object_widget_handle (SheetObject *so, GtkWidget *widget,
				 GnomeCanvasItem *item);

SheetObject *sheet_widget_label_new		(Sheet *sheet);
SheetObject *sheet_widget_frame_new		(Sheet *sheet);
SheetObject *sheet_widget_button_new		(Sheet *sheet);
SheetObject *sheet_widget_checkbox_new		(Sheet *sheet);
SheetObject *sheet_widget_radio_button_new	(Sheet *sheet);
SheetObject *sheet_widget_list_new		(Sheet *sheet);
SheetObject *sheet_widget_combo_new		(Sheet *sheet);

#endif /* GNUMERIC_SHEET_OBJECT_WIDGET_H */

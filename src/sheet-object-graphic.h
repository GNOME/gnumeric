#ifndef GNUMERIC_SHEET_OBJECT_GRAPHIC_H
#define GNUMERIC_SHEET_OBJECT_GRAPHIC_H

#include "sheet-object.h"

SheetObject *sheet_object_line_new  (Sheet *sheet, gboolean with_arrow);
SheetObject *sheet_object_box_new   (Sheet *sheet, gboolean is_oval);

#endif /* GNUMERIC_SHEET_OBJECT_GRAPHIC_H */

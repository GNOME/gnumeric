#ifndef GNUMERIC_APPLICATION_H
#define GNUMERIC_APPLICATION_H

#include "workbook.h"

void         application_init			(void);

void         application_clipboard_clear	(void);

void         application_clipboard_copy		(Sheet *sheet, Range const *area);
void         application_clipboard_cut		(Sheet *sheet, Range const *area);

void	     application_clipboard_unant        (void);
gboolean     application_clipboard_is_empty	(void);
Sheet *      application_clipboard_sheet_get	(void);
CellRegion * application_clipboard_contents_get	(void);
Range const* application_clipboard_area_get	(void);

#endif /* GNUMERIC_APPLICATION_H */

#ifndef GNUMERIC_APPLICATION_H
#define GNUMERIC_APPLICATION_H

#include "gnumeric.h"

void         application_init			(void);

Workbook *   application_workbook_get_by_name   (char const * const name);
Workbook *   application_workbook_get_by_index  (int i);

void         application_clipboard_clear	(gboolean drop_selection);

void         application_clipboard_copy		(Sheet *sheet, Range const *area);
void         application_clipboard_cut		(Sheet *sheet, Range const *area);

void	     application_clipboard_unant        (void);
gboolean     application_clipboard_is_empty	(void);
Sheet *      application_clipboard_sheet_get	(void);
CellRegion * application_clipboard_contents_get	(void);
Range const* application_clipboard_area_get	(void);
GList *	     application_history_get_list	(void);
gchar *	     application_history_update_list	(gchar *);
gchar *	     application_history_list_shrink	(void);
void 	     application_history_write_config 	(void);


float	     application_display_dpi_get (gboolean const horizontal);
void 	     application_display_dpi_set (gboolean const horizontal, float const);
float	     application_dpi_to_pixels ();

gboolean     application_use_auto_complete_get (void);
void         application_use_auto_complete_set (gboolean use_auto_complete);

#endif /* GNUMERIC_APPLICATION_H */

#ifndef GNUMERIC_APPLICATION_H
#define GNUMERIC_APPLICATION_H

#include "gnumeric.h"

void         application_init			(void);

void         application_workbook_list_add (Workbook *wb);
void         application_workbook_list_remove (Workbook *wb);
GList *      application_workbook_list (void);

Workbook *   application_workbook_get_by_name   (char const * const name);
Workbook *   application_workbook_get_by_index  (int i);
typedef gboolean (*WorkbookCallback)(Workbook *, gpointer data);
gboolean     application_workbook_foreach  (WorkbookCallback cback,
					    gpointer data);


void         application_clipboard_clear	(gboolean drop_selection);
void         application_clipboard_copy		(WorkbookControl *wbc,
						 Sheet *sheet, Range const *area);
void         application_clipboard_cut		(WorkbookControl *wbc,
						 Sheet *sheet, Range const *area);
void	     application_clipboard_unant        (void);
gboolean     application_clipboard_is_empty	(void);
gboolean     application_clipboard_is_cut       (void);
Sheet *      application_clipboard_sheet_get	(void);
CellRegion * application_clipboard_contents_get	(void);
Range const* application_clipboard_area_get	(void);

GList *	     application_history_get_list	(void);
gchar *	     application_history_update_list	(const gchar *);
gchar *	     application_history_list_shrink	(void);
void 	     application_history_write_config 	(void);


double	     application_display_dpi_get (gboolean horizontal);
void 	     application_display_dpi_set (gboolean horizontal, double);
double	     application_dpi_to_pixels (void);

gboolean     application_use_auto_complete    (void);
gboolean     application_live_scrolling	      (void);
int	     application_auto_expr_recalc_lag (void);

#endif /* GNUMERIC_APPLICATION_H */

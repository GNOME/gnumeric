#ifndef GNUMERIC_APPLICATION_H
#define GNUMERIC_APPLICATION_H

#include "gnumeric.h"
#include <gconf/gconf-client.h>
#include <glib-object.h>

#define GNUMERIC_APPLICATION_TYPE (gnumeric_application_get_type ())
#define GNUMERIC_APPLICATION(o) (G_TYPE_CHECK_INSTANCE_CAST((o), GNUMERIC_APPLICATION_TYPE, GnumericApplication))
#define GNUMERIC_APPLICATION_CLASS(k) (G_TYPE_CHECK_CLASS_CAST((k), GNUMERIC_APPLICATION__TYPE, GnumericApplicationClass))
#define IS_GNUMERIC_APPLICATION(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), GNUMERIC_APPLICATION_TYPE))
#define IS_GNUMERIC_APPLICATION_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE((k), GNUMERIC_APPLICATION_TYPE))

typedef struct _GnumericApplication GnumericApplication;

GType	     gnumeric_application_get_type (void);

void         application_release_gconf_client   (void);
GConfClient *application_get_gconf_client       (void);

void         application_release_pref_dialog    (void);
gpointer     application_get_pref_dialog        (void);
void         application_set_pref_dialog        (gpointer dialog);


void         application_workbook_list_add (Workbook *wb);
void         application_workbook_list_remove (Workbook *wb);
GList *      application_workbook_list (void);

Workbook *   application_workbook_get_by_name   (char const * const name);
Workbook *   application_workbook_get_by_index  (int i);
typedef gboolean (*WorkbookCallback)(Workbook *, gpointer data);
gboolean     application_workbook_foreach  (WorkbookCallback cback,
					    gpointer data);


void         application_clipboard_clear	(gboolean drop_selection);
void         application_clipboard_cut_copy	(WorkbookControl *wbc, gboolean is_cut,
						 SheetView *sv, Range const *area,
						 gboolean animate_range);
void	     application_clipboard_unant          (void);
gboolean     application_clipboard_is_empty	  (void);
gboolean     application_clipboard_is_cut         (void);
Sheet *      application_clipboard_sheet_get	  (void);
SheetView *  application_clipboard_sheet_view_get (void);
CellRegion * application_clipboard_contents_get	  (void);
Range const* application_clipboard_area_get	  (void);

GSList *     application_history_get_list	(void);
gchar *	     application_history_update_list	(const gchar *);
gchar *	     application_history_list_shrink	(void);
void 	     application_history_write_config 	(void);


double	     application_display_dpi_get (gboolean horizontal);
double	     application_dpi_to_pixels (void);

gboolean     application_use_auto_complete    (void);
gboolean     application_live_scrolling	      (void);
int	     application_auto_expr_recalc_lag (void);

#endif /* GNUMERIC_APPLICATION_H */

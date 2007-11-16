/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_APPLICATION_H_
# define _GNM_APPLICATION_H_

#include "gnumeric.h"
#include <glib-object.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

G_BEGIN_DECLS

#define GNM_APP_TYPE	(gnm_app_get_type ())
typedef gboolean (*GnmWbIterFunc) (Workbook *, gpointer data);

GType	     gnm_app_get_type (void);
GObject     *gnm_app_get_app (void);

/* List of active workbooks */
void         gnm_app_workbook_list_add     (Workbook *wb);
void         gnm_app_workbook_list_remove  (Workbook *wb);
GList *      gnm_app_workbook_list 	   (void);
Workbook    *gnm_app_workbook_get_by_uri   (char const *uri);
Workbook    *gnm_app_workbook_get_by_name  (char const *name,
					    char const *ref_uri);
Workbook    *gnm_app_workbook_get_by_index (int i);
gboolean     gnm_app_workbook_foreach	   (GnmWbIterFunc func, gpointer data);

GSList      *gnm_app_history_get_list	   (int max_elements);
void	     gnm_app_history_add	   (char const *filename, const char *mimetype);

/* GtkFileFilter */
void        *gnm_app_create_opener_filter (void);

/* Prefs */
int	     gnm_app_enter_moves_dir	  (void);
gboolean     gnm_app_use_auto_complete    (void);
gboolean     gnm_app_use_transition_keys  (void);
void         gnm_app_set_transition_keys  (gboolean);
gboolean     gnm_app_live_scrolling	  (void);
int	     gnm_app_auto_expr_recalc_lag (void);

double	     gnm_app_display_dpi_get	  (gboolean horizontal);
double	     gnm_app_dpi_to_pixels	  (void);

/* Clipboard */
void		 gnm_app_clipboard_clear	  (gboolean drop_selection);
void		 gnm_app_clipboard_invalidate_sheet (Sheet *sheet);
void		 gnm_app_clipboard_cut_copy	  (WorkbookControl *wbc, gboolean is_cut,
						   SheetView *sv, GnmRange const *area,
						   gboolean animate_range);
void		 gnm_app_clipboard_cut_copy_obj	  (WorkbookControl *wbc, gboolean is_cut,
						   SheetView *sv, GSList *objects);
void		 gnm_app_clipboard_unant	  (void);
gboolean	 gnm_app_clipboard_is_empty	  (void);
gboolean	 gnm_app_clipboard_is_cut	  (void);
Sheet		*gnm_app_clipboard_sheet_get	  (void);
SheetView	*gnm_app_clipboard_sheet_view_get (void);
GnmCellRegion	*gnm_app_clipboard_contents_get	  (void);
GnmRange const	*gnm_app_clipboard_area_get	  (void);

/**********************************************************************
 * Temporary home for extra actions until we rework this in 1.5
 * with libgoffice
 **/

typedef void (*GnmActionHandler) (GnmAction const *action, WorkbookControl *wbc,
				  gpointer user_data);
struct _GnmAction {
	char *id;	 /* id of the function that will handle this */
	char *label;	 /* untranslated, gettext domain will be passed later */
	char *icon_name; /* optionally NULL */
	/* simplistic for now :
	 * is the action always available (File -> New) or only available
	 * when we are not editing (Cell -> Format)
	 * Later on this needs to be more comprehensive with things like
	 * per-sheetobject flags
	 **/
	gboolean always_available;

	GnmActionHandler	handler;
};
typedef struct {
	GSList	   *actions;
	char	   *layout;
	char const *domain;
	gpointer    user_data;
} GnmAppExtraUI;

GnmAction *gnm_action_new  (char const *name, char const *label,
			    char const *icon, gboolean always_available,
			    GnmActionHandler handler);
void	   gnm_action_free (GnmAction *action);

GnmAppExtraUI *gnm_app_add_extra_ui (GSList *actions, char *layout,
				     char const *domain,
				     gpointer user_data);
void	   gnm_app_remove_extra_ui  (GnmAppExtraUI *extra_ui);
void	   gnm_app_foreach_extra_ui (GFunc func, gpointer data);

/**********************************************************************/

/* internal implementation util */
void _gnm_app_flag_windows_changed (void);

G_END_DECLS

#endif /* _GNM_APPLICATION_H_ */

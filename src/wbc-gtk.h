#ifndef _GNM_WBC_GTK_H_
# define _GNM_WBC_GTK_H_

#include <gnumeric.h>
#include <gnumeric-fwd.h>
#include <workbook-control.h>
#include <widgets/gnm-expr-entry.h>


G_BEGIN_DECLS

#define GNM_WBC_GTK_TYPE	(wbc_gtk_get_type ())
#define WBC_GTK(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_WBC_GTK_TYPE, WBCGtk))
#define GNM_IS_WBC_GTK(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_WBC_GTK_TYPE))

GType   wbc_gtk_get_type (void);
WBCGtk *wbc_gtk_new (WorkbookView *optional_view,
		     Workbook	  *optional_wb,
		     GdkScreen	  *optional_screen,
		     const gchar  *optional_geometry);

GtkWindow	*wbcg_toplevel	  (WBCGtk *wbcg);
void	         wbcg_set_transient (WBCGtk *wbcg,
				     GtkWindow *window);
SheetControlGUI *wbcg_get_nth_scg (WBCGtk *wbcg, int i);
SheetControlGUI *wbcg_cur_scg	  (WBCGtk *wbcg);
Sheet		*wbcg_cur_sheet	  (WBCGtk *wbcg);
Sheet		*wbcg_focus_cur_scg (WBCGtk *wbcg);
int              wbcg_get_n_scg   (WBCGtk const *wbcg);

gboolean   wbcg_rangesel_possible (WBCGtk const *wbcg);
gboolean   wbcg_is_editing	  (WBCGtk const *wbcg);
void	   wbcg_set_status_text	  (WBCGtk *wbcg,
				   char const *text);
void       wbcg_toggle_visibility (WBCGtk *wbcg,
				   GtkToggleAction *action);
void       wbcg_copy_toolbar_visibility (WBCGtk *new_wbcg,
					 WBCGtk *wbcg);

void       wbcg_set_end_mode      (WBCGtk *wbcg, gboolean flag);

PangoFontDescription *wbcg_get_font_desc (WBCGtk *wbcg);

WBCGtk *wbcg_find_for_workbook (Workbook *wb,
					    WBCGtk *candidate,
					    GdkScreen *pref_screen,
					    GdkDisplay *pref_display);

typedef enum {
	WBC_EDIT_REJECT = 0,
	WBC_EDIT_ACCEPT,	/* assign content to current edit pos */
	WBC_EDIT_ACCEPT_WO_AC,	/* assign content to current edit pos not */
	                        /* autocorrecting*/
	WBC_EDIT_ACCEPT_RANGE,	/* assign content to first range in selection */
	WBC_EDIT_ACCEPT_ARRAY	/* assign content as an array to the first range in selection */
} WBCEditResult;

gboolean wbcg_edit_finish (WBCGtk *wbcg, WBCEditResult result,
			   gboolean *showed_dialog);
gboolean wbcg_edit_start  (WBCGtk *wbcg,
			   gboolean blankp, gboolean cursorp);

void	    wbcg_insert_object		(WBCGtk *wbcg, SheetObject *so);
void	    wbcg_insert_object_clear	(WBCGtk *wbcg);

void	    wbc_gtk_detach_guru		(WBCGtk *wbcg);
void	    wbc_gtk_attach_guru		(WBCGtk *wbcg, GtkWidget *guru);
void	    wbc_gtk_attach_guru_with_unfocused_rs (WBCGtk *wbcg, GtkWidget *guru,
						   GnmExprEntry *gee);
GtkWidget  *wbc_gtk_get_guru		(WBCGtk const *wbcg);

void	    wbcg_auto_complete_destroy  (WBCGtk *wbcg);
char const *wbcg_edit_get_display_text	(WBCGtk *wbcg);
void	    wbcg_edit_add_markup	(WBCGtk *wbcg, PangoAttribute *attr);
PangoAttrList *wbcg_edit_get_markup	(WBCGtk *wbcg, gboolean full);

GtkEntry     *wbcg_get_entry		(WBCGtk const *wbcg);
GnmExprEntry *wbcg_get_entry_logical	(WBCGtk const *wbcg);
GtkWidget    *wbcg_get_entry_underlying	(WBCGtk const *wbcg);
void	      wbcg_set_entry		(WBCGtk *wbc,
					 GnmExprEntry *new_entry);
gboolean      wbcg_entry_has_logical	(WBCGtk const *wbcg);

void          wbcg_focus_current_cell_indicator (WBCGtk const *wbcg);

G_END_DECLS

#endif /* _GNM_WBC_GTK_H_ */

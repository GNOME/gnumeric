#ifndef GNUMERIC_EXPR_ENTRY_H
#define GNUMERIC_EXPR_ENTRY_H

#include "gui-gnumeric.h"
#include <gtk/gtkentry.h>

#define GNM_EXPR_ENTRY_TYPE	(gnm_expr_entry_get_type ())
#define GNM_EXPR_ENTRY(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_EXPR_ENTRY_TYPE, GnmExprEntry))
#define IS_GNM_EXPR_ENTRY(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_EXPR_ENTRY_TYPE))

typedef struct _GnmExprEntry GnmExprEntry;

typedef enum {
	GNM_EE_SINGLE_RANGE    = 1 << 0,
	GNM_EE_ABS_COL         = 1 << 1,
	GNM_EE_ABS_ROW         = 1 << 2,
	GNM_EE_FULL_COL        = 1 << 3,
	GNM_EE_FULL_ROW        = 1 << 4,
	GNM_EE_SHEET_OPTIONAL  = 1 << 5,
	GNM_EE_MASK            = 0x3F
} GnmExprEntryFlags;

GType gnm_expr_entry_get_type (void);
GnmExprEntry *gnm_expr_entry_new (WorkbookControlGUI *wbcg,
				       gboolean with_icon);

/* Widget specific methods */
void	  gnm_expr_entry_freeze 	(GnmExprEntry *e);
void	  gnm_expr_entry_thaw		(GnmExprEntry *e);
void	  gnm_expr_entry_set_absolute	(GnmExprEntry *e);
void	  gnm_expr_entry_set_flags	(GnmExprEntry *e,
					 GnmExprEntryFlags flags,
					 GnmExprEntryFlags mask);
void	  gnm_expr_entry_set_scg	(GnmExprEntry *e,
					 SheetControlGUI *scg);
GtkEntry *gnm_expr_entry_get_entry	(GnmExprEntry *e);
gboolean  gnm_expr_entry_get_rangesel	(GnmExprEntry *e,
					 Range *r, Sheet **sheet);
void	  gnm_expr_expr_find_range	(GnmExprEntry *e);
void	  gnm_expr_entry_rangesel_stop	(GnmExprEntry *e,
					 gboolean clear_string);

gboolean  gnm_expr_entry_can_rangesel	(GnmExprEntry *e);
gboolean  gnm_expr_entry_is_blank	(GnmExprEntry *e);
gboolean  gnm_expr_entry_is_cell_ref	(GnmExprEntry *e, 
					 Sheet *sheet,
					 gboolean allow_multiple_cell);

char const *gnm_expr_entry_get_text	(GnmExprEntry const *ee);
Value	 *gnm_expr_entry_parse_as_value	(GnmExprEntry *ee, Sheet *sheet);
GSList	 *gnm_expr_entry_parse_as_list	(GnmExprEntry *ee, Sheet *sheet);
GnmExpr const  *gnm_expr_entry_parse	(GnmExprEntry *e,
					 ParsePos const *pp,
					 ParseError *perr, gboolean start_sel);
char     *gnm_expr_entry_global_range_name (GnmExprEntry *e, Sheet *sheet);
void	  gnm_expr_entry_load_from_text	(GnmExprEntry *e, char const *str);
void	  gnm_expr_entry_load_from_dep	(GnmExprEntry *e,
					 Dependent const *dep);
void	  gnm_expr_entry_load_from_expr	(GnmExprEntry *e,
					 GnmExpr const *expr,
					 ParsePos const *pp);
gboolean  gnm_expr_entry_load_from_range (GnmExprEntry *e,
					  Sheet *sheet, Range const *r);

void gnm_expr_entry_set_update_policy (GnmExprEntry *e,
					    GtkUpdateType  policy);
void gnm_expr_entry_grab_focus (GnmExprEntry *e, gboolean select_all);

/* Cell Renderer Specific Method */

gboolean gnm_expr_entry_editing_canceled (GnmExprEntry *e);

/* private : for internal use */
void gnm_expr_entry_signal_update (GnmExprEntry *gee, gboolean user_requested);

#endif /* GNUMERIC_EXPR_ENTRY_H */

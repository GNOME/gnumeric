#ifndef GNUMERIC_EXPR_ENTRY_H
#define GNUMERIC_EXPR_ENTRY_H

#include "gui-gnumeric.h"
#include "parse-util.h"
#include <gtk/gtkentry.h>

#define GNM_EXPR_ENTRY_TYPE	(gnm_expr_entry_get_type ())
#define GNM_EXPR_ENTRY(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_EXPR_ENTRY_TYPE, GnmExprEntry))
#define IS_GNM_EXPR_ENTRY(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_EXPR_ENTRY_TYPE))

typedef struct _GnmExprEntry GnmExprEntry;

typedef enum {
	GNM_EE_SINGLE_RANGE    = 1 << 0,
	GNM_EE_FORCE_ABS_REF   = 1 << 1, /* takes precedence over FORCE_REL_REF */
	GNM_EE_FORCE_REL_REF   = 1 << 2,
	GNM_EE_FULL_COL        = 1 << 3,
	GNM_EE_FULL_ROW        = 1 << 4,
	GNM_EE_SHEET_OPTIONAL  = 1 << 5,
	GNM_EE_FORMULA_ONLY    = 1 << 6,
	GNM_EE_MASK            = 0x7F
} GnmExprEntryFlags;

GType gnm_expr_entry_get_type (void);
GnmExprEntry *gnm_expr_entry_new       (WBCGtk *wbcg,
					gboolean with_icon);
GtkWidget    *gnm_expr_entry_new_glade (void);

/* Widget specific methods */
void	  gnm_expr_entry_freeze 	(GnmExprEntry *gee);
void	  gnm_expr_entry_thaw		(GnmExprEntry *gee);
void	  gnm_expr_entry_set_flags	(GnmExprEntry *gee,
					 GnmExprEntryFlags flags,
					 GnmExprEntryFlags mask);
void	  gnm_expr_entry_set_scg	(GnmExprEntry *gee,
					 SheetControlGUI *scg);
GtkEntry *gnm_expr_entry_get_entry	(GnmExprEntry *gee);
gboolean  gnm_expr_entry_get_rangesel	(GnmExprEntry const *gee,
					 GnmRange *r, Sheet **sheet);
void	  gnm_expr_entry_find_range	(GnmExprEntry *gee);
void	  gnm_expr_entry_rangesel_start	(GnmExprEntry *gee);
void	  gnm_expr_entry_rangesel_stop	(GnmExprEntry *gee,
					 gboolean clear_string);

gboolean  gnm_expr_entry_can_rangesel	(GnmExprEntry *gee);
gboolean  gnm_expr_entry_is_blank	(GnmExprEntry *gee);
gboolean  gnm_expr_entry_is_cell_ref	(GnmExprEntry *gee,
					 Sheet *sheet,
					 gboolean allow_multiple_cell);

char const *gnm_expr_entry_get_text	  (GnmExprEntry const *gee);
GnmValue   *gnm_expr_entry_parse_as_value (GnmExprEntry *gee, Sheet *sheet);
GSList	   *gnm_expr_entry_parse_as_list  (GnmExprEntry *gee, Sheet *sheet);
GnmExprTop const *gnm_expr_entry_parse	  (GnmExprEntry *gee,
					   GnmParsePos const *pp,
					   GnmParseError *perr, gboolean start_sel,
					   GnmExprParseFlags flags);
char    *gnm_expr_entry_global_range_name (GnmExprEntry *gee, Sheet *sheet);
void	 gnm_expr_entry_load_from_text	  (GnmExprEntry *gee, char const *str);
void	 gnm_expr_entry_load_from_dep	  (GnmExprEntry *gee,
					   GnmDependent const *dep);
void	 gnm_expr_entry_load_from_expr	  (GnmExprEntry *gee,
					   GnmExprTop const *texpr,
					   GnmParsePos const *pp);
gboolean gnm_expr_entry_load_from_range   (GnmExprEntry *gee,
					   Sheet *sheet, GnmRange const *r);

void gnm_expr_entry_set_update_policy (GnmExprEntry *gee,
					    GtkUpdateType  policy);
void gnm_expr_entry_grab_focus (GnmExprEntry *gee, gboolean select_all);

void    gnm_expr_entry_disable_highlight (GnmExprEntry *gee);
void    gnm_expr_entry_enable_highlight  (GnmExprEntry *gee);

/* Cell Renderer Specific Method */

gboolean gnm_expr_entry_editing_canceled (GnmExprEntry *gee);

/* private : for internal use */
void gnm_expr_entry_signal_update (GnmExprEntry *gee, gboolean user_requested);

#endif /* GNUMERIC_EXPR_ENTRY_H */

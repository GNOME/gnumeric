#ifndef GNUMERIC_EXPR_ENTRY_H
#define GNUMERIC_EXPR_ENTRY_H

#include "gui-gnumeric.h"
#include <gtk/gtkentry.h>

#define GNUMERIC_TYPE_EXPR_ENTRY	 (gnumeric_expr_entry_get_type ())
#define GNUMERIC_EXPR_ENTRY(obj)	 (G_TYPE_CHECK_INSTANCE_CAST ((obj), GNUMERIC_TYPE_EXPR_ENTRY, GnumericExprEntry))
#define GNUMERIC_EXPR_ENTRY_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), GNUMERIC_TYPE_EXPR_ENTRY, GnumericExprEntryClass))
#define IS_GNUMERIC_EXPR_ENTRY(obj)	 (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GNUMERIC_TYPE_EXPR_ENTRY))

typedef struct _GnumericExprEntry GnumericExprEntry;

typedef enum
{
	GNUM_EE_SINGLE_RANGE    = 1 << 0,
	GNUM_EE_ABS_COL         = 1 << 1,
	GNUM_EE_ABS_ROW         = 1 << 2,
	GNUM_EE_FULL_COL        = 1 << 3,
	GNUM_EE_FULL_ROW        = 1 << 4,
	GNUM_EE_SHEET_OPTIONAL  = 1 << 5,
	GNUM_EE_MASK            = 0x3F
} GnumericExprEntryFlags;

GType gnumeric_expr_entry_get_type (void);
GnumericExprEntry *gnumeric_expr_entry_new (WorkbookControlGUI *wbcg,
					    gboolean with_icon);

/* Widget specific methods */
void	  gnm_expr_entry_freeze 	(GnumericExprEntry *e);
void	  gnm_expr_entry_thaw		(GnumericExprEntry *e);
void	  gnm_expr_entry_set_absolute	(GnumericExprEntry *e);
void	  gnm_expr_entry_set_flags	(GnumericExprEntry *e,
					 GnumericExprEntryFlags flags,
					 GnumericExprEntryFlags mask);
void	  gnm_expr_entry_set_scg	(GnumericExprEntry *e,
					 SheetControlGUI *scg);
GtkEntry *gnm_expr_entry_get_entry	(GnumericExprEntry *e);
void	  gnm_expr_entry_get_rangesel	(GnumericExprEntry *e,
					 Range *r, Sheet **sheet);
void	  gnm_expr_entry_rangesel_start	(GnumericExprEntry *e);
void	  gnm_expr_entry_rangesel_stop	(GnumericExprEntry *e,
					 gboolean clear_string);

gboolean  gnm_expr_entry_can_rangesel	(GnumericExprEntry *e);
gboolean  gnm_expr_entry_is_blank	(GnumericExprEntry *e);
gboolean  gnm_expr_entry_is_cell_ref (GnumericExprEntry *e, 
				      Sheet *sheet, gboolean allow_multiple_cell);

Value	 *gnm_expr_entry_parse_as_value	(GnumericExprEntry *ee, Sheet *sheet);
GSList	 *gnm_expr_entry_parse_as_list	(GnumericExprEntry *ee, Sheet *sheet);
ExprTree *gnm_expr_entry_parse		(GnumericExprEntry *e,
					 ParsePos const *pp,
					 ParseError *perr, gboolean start_sel);
char     *gnm_expr_entry_global_range_name (GnumericExprEntry *e, Sheet *sheet);
void	 gnm_expr_entry_load_from_text	(GnumericExprEntry *e, char const *str);
void	 gnm_expr_entry_load_from_dep	(GnumericExprEntry *e,
					 Dependent const *dep);
void	 gnm_expr_entry_load_from_expr	(GnumericExprEntry *e,
					 ExprTree const *expr,
					 ParsePos const *pp);
gboolean gnm_expr_entry_load_from_range (GnumericExprEntry *e,
					 Sheet *sheet, Range const *r);

void gnumeric_expr_entry_set_update_policy (GnumericExprEntry *e,
					    GtkUpdateType  policy);

/* private : for internal use */
void gnm_expr_entry_end_of_drag	(GnumericExprEntry *gee);

#endif /* GNUMERIC_EXPR_ENTRY_H */

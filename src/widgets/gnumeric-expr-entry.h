#ifndef GNUMERIC_EXPR_ENTRY_H
#define GNUMERIC_EXPR_ENTRY_H

#include "gui-gnumeric.h"
#include <gtk/gtkwidget.h>

#define GNUMERIC_TYPE_EXPR_ENTRY	 (gnumeric_expr_entry_get_type ())
#define GNUMERIC_EXPR_ENTRY(obj)	 (GTK_CHECK_CAST ((obj), GNUMERIC_TYPE_EXPR_ENTRY, GnumericExprEntry))
#define GNUMERIC_EXPR_ENTRY_CLASS(klass) (GTK_CHECK_CLASS_CAST ((klass), GNUMERIC_TYPE_EXPR_ENTRY, GnumericExprEntryClass))
#define IS_GNUMERIC_EXPR_ENTRY(obj)	 (GTK_CHECK_TYPE ((obj), GNUMERIC_TYPE_EXPR_ENTRY))

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

/* Standard Gtk functions */
GtkType	   gnumeric_expr_entry_get_type (void);
GtkWidget *gnumeric_expr_entry_new (WorkbookControlGUI *wbcg);

/* Widget specific methods */
void gnumeric_expr_entry_freeze 	(GnumericExprEntry *expr_entry);
void gnumeric_expr_entry_thaw		(GnumericExprEntry *expr_entry);
void gnumeric_expr_entry_set_flags	(GnumericExprEntry *expr_entry,
					 GnumericExprEntryFlags flags,
					 GnumericExprEntryFlags mask);
void gnumeric_expr_entry_set_scg	(GnumericExprEntry *expr_entry,
					 SheetControlGUI *scg);
void gnumeric_expr_entry_get_rangesel	(GnumericExprEntry *expr_entry,
					 Range *r, Sheet **sheet);
void gnumeric_expr_entry_rangesel_start (GnumericExprEntry *expr_entry);
void gnumeric_expr_entry_rangesel_stop  (GnumericExprEntry *expr_entry,
					 gboolean clear_string);
gboolean gnumeric_expr_entry_set_range	(GnumericExprEntry *expr_entry,
					 Sheet *sheet, Range const *r);

void	 gnumeric_expr_entry_clear 		     (GnumericExprEntry *gee);
void	 gnumeric_expr_entry_set_rangesel_from_dep   (GnumericExprEntry *gee,
						      Dependent const *dep);


/* Convenience functions */
void gnumeric_expr_entry_set_absolute (GnumericExprEntry *expr_entry);

/* Is a range selection meaningful here? */
gboolean  gnumeric_expr_entry_rangesel_meaningful (GnumericExprEntry *expr_entry);

#endif

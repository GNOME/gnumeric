#ifndef GNUMERIC_EXPR_ENTRY_H
#define GNUMERIC_EXPR_ENTRY_H

#include <gtk/gtkentry.h>
#include "gui-gnumeric.h"

#define GNUMERIC_TYPE_EXPR_ENTRY\
    (gnumeric_expr_entry_get_type ())
#define GNUMERIC_EXPR_ENTRY(obj)\
    (GTK_CHECK_CAST ((obj), GNUMERIC_TYPE_EXPR_ENTRY, GnumericExprEntry))
#define GNUMERIC_EXPR_ENTRY_CLASS(klass)\
    (GTK_CHECK_CLASS_CAST ((klass), GNUMERIC_TYPE_EXPR_ENTRY, GnumericExprEntryClass))
#define GNUMERIC_IS_EXPR_ENTRY(obj)\
    (GTK_CHECK_TYPE ((obj), GNUMERIC_TYPE_EXPR_ENTRY))
#define GNUMERIC_IS_EXPR_ENTRY_CLASS(klass)\
    (GTK_CHECK_CLASS_TYPE ((klass), GNUMERIC_TYPE_EXPR_ENTRY))


typedef struct _GnumericExprEntry GnumericExprEntry;
typedef struct _GnumericExprEntryPrivate GnumericExprEntryPrivate;
typedef struct _GnumericExprEntryClass GnumericExprEntryClass;

struct _GnumericExprEntryClass {
	GtkEntryClass parent_class;
};

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
GtkType gnumeric_expr_entry_get_type (void);
GtkWidget *gnumeric_expr_entry_new (void);

/* Widget specific methods */
void gnumeric_expr_entry_set_flags (GnumericExprEntry *expr_entry,
				    GnumericExprEntryFlags flags,
				    GnumericExprEntryFlags mask);

void gnumeric_expr_entry_set_scg (GnumericExprEntry *expr_entry,
				  SheetControlGUI *scg);

void gnumeric_expr_entry_set_rangesel_from_text (GnumericExprEntry *expr_entry,
						 char *text);

void
gnumeric_expr_entry_set_rangesel_from_range (GnumericExprEntry *expr_entry,
					     Range *r, Sheet *sheet, int pos);

void gnumeric_expr_entry_rangesel_stopped (GnumericExprEntry *expr_entry,
					   gboolean clear_string);

/* Convenience functions */
void gnumeric_expr_entry_set_absolute (GnumericExprEntry *expr_entry);
void gnumeric_expr_entry_toggle_absolute (GnumericExprEntry *expr_entry);

/* Is this GtkEntry editing at a subexpression boundary */
gboolean  gnumeric_expr_entry_at_subexpr_boundary_p (GnumericExprEntry *entry);


#endif

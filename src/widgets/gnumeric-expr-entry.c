/*
 * gnumeric-expr-entry.c: An entry widget specialized to handle expressions
 * and ranges. 
 *
 * Author:
 *   Jon Kåre Hellan (hellan@acm.org)
 */

#include <config.h>
#include "gnumeric-expr-entry.h"
#include "sheet-control-gui.h"
#include "sheet-merge.h"
#include "parse-util.h"
#include "ranges.h"
#include "value.h"
#include "sheet.h"

static GtkObjectClass *gnumeric_expr_entry_parent_class;

typedef struct {
	Range range;
	Sheet *sheet;
	int text_start;
	int text_end;
} Rangesel;

struct _GnumericExprEntry {
	GtkEntry entry;
	SheetControlGUI *scg;
	GnumericExprEntryFlags flags;
	int freeze_count;
	int pos;
	guint id_cb_scg_destroy;
	Sheet *target_sheet;
	Rangesel rangesel;
};

static char * make_rangesel_text (GnumericExprEntry *expr_entry);

static void update_rangesel_text (GnumericExprEntry *expr_entry, char *text);

static void
gnumeric_expr_entry_finalize (GtkObject *object)
{
	GnumericExprEntry *expr_entry = GNUMERIC_EXPR_ENTRY (object);

	if (expr_entry->scg)
		gtk_signal_disconnect (GTK_OBJECT (expr_entry->scg),
				       expr_entry->id_cb_scg_destroy);

	GTK_OBJECT_CLASS (gnumeric_expr_entry_parent_class)->finalize (object);
}

static void
gnumeric_expr_entry_class_init (GtkObjectClass *object_class)
{
	gnumeric_expr_entry_parent_class
		= gtk_type_class (gtk_entry_get_type());

	object_class->finalize = gnumeric_expr_entry_finalize;
}

GtkType
gnumeric_expr_entry_get_type (void)
{
	static GtkType type = 0;

	if (!type){
		GtkTypeInfo info = {
			"GnumericExprEntry",
			sizeof (GnumericExprEntry),
			sizeof (GnumericExprEntryClass),
			(GtkClassInitFunc) gnumeric_expr_entry_class_init,
			(GtkObjectInitFunc) NULL,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		type = gtk_type_unique (gtk_entry_get_type (), &info);
	}

	return type;
}

/**
 * gnumeric_expr_entry_new:
 * 
 * Creates a new #GnumericExprEntry, which is an entry widget with support
 * for range selections.
 * The entry is created with default flag settings which are suitable for use
 * in many dialogs, but see #gnumeric_expr_entry_set_flags.
 * 
 * Return value: a new #GnumericExprEntry.
 **/
GtkWidget *
gnumeric_expr_entry_new ()
{
	GnumericExprEntry *expr_entry;

	expr_entry = gtk_type_new (gnumeric_expr_entry_get_type ());

	expr_entry->flags |= GNUM_EE_SINGLE_RANGE;

	return GTK_WIDGET (expr_entry);
}

void
gnumeric_expr_entry_freeze (GnumericExprEntry *expr_entry)
{
	g_return_if_fail (expr_entry != NULL);
	g_return_if_fail (GNUMERIC_IS_EXPR_ENTRY (expr_entry));
			  
	expr_entry->freeze_count++;
}

void
gnumeric_expr_entry_thaw (GnumericExprEntry *expr_entry)
{
	g_return_if_fail (expr_entry != NULL);
	g_return_if_fail (GNUMERIC_IS_EXPR_ENTRY (expr_entry));

	if (expr_entry->freeze_count > 0)
		if ((--expr_entry->freeze_count) == 0) { 
			char *rangesel_text;

			/* Update text */
			rangesel_text = make_rangesel_text (expr_entry);
			
			update_rangesel_text (expr_entry, rangesel_text);
			
			g_free (rangesel_text);
		}
}

static void
reset_rangesel (GnumericExprEntry *expr_entry)
{
	Rangesel *rs = &expr_entry->rangesel;
	
	rs->text_start = 0;
	rs->text_end = 0;

	memset (&rs->range, 0, sizeof (Range));
	rs->sheet = NULL;
	expr_entry->flags &= ~GNUM_EE_ABS_COL;
	expr_entry->flags &= ~GNUM_EE_ABS_ROW;
}

static void
cb_scg_destroy (SheetControlGUI *scg, GnumericExprEntry *expr_entry)
{
	g_return_if_fail (scg == expr_entry->scg);
	gnumeric_expr_entry_set_scg (expr_entry, NULL);	
}
	
/**
 * gnumeric_expr_entry_set_flags:
 * @expr_entry: a #GnumericExprEntry
 * @flags:      bitmap of flag values
 * @mask:       bitmap with ones for flags to be changed
 * 
 * Changes the flags specified in @mask to values given in @flags.
 * 
 * Flags (%FALSE by default, with exceptions given below):
 * %GNUM_EE_SINGLE_RANGE      Entry will only hold a single range.
 *                            %TRUE by default
 * %GNUM_EE_ABS_COL           Column reference is absolute.
 * %GNUM_EE_ABS_ROW           Row reference is absolute.
 * %GNUM_EE_FULL_COL          Range consists of full columns.
 * %GNUM_EE_FULL_ROW          Range consists of full rows.
 * %GNUM_EE_SHEET_OPTIONAL    Sheet must not be displayed if current sheet.
 **/
void
gnumeric_expr_entry_set_flags (GnumericExprEntry *expr_entry,
			       GnumericExprEntryFlags flags,
			       GnumericExprEntryFlags mask)
{
	g_return_if_fail (GNUMERIC_IS_EXPR_ENTRY (expr_entry));

	expr_entry->flags = (expr_entry->flags & ~mask) | (flags & mask);
}

/**
 * gnumeric_expr_entry_set_scg
 * @expr_entry: a #GnumericExprEntry
 * @scg:        a #SheetControlGUI
 * 
 * Associates the entry with a SheetControlGUI. The entry widget
 * automatically removes the association when the SheetControlGUI is
 * destroyed.
 **/
void
gnumeric_expr_entry_set_scg (GnumericExprEntry *expr_entry,
			     SheetControlGUI *scg)
{
	g_return_if_fail (GNUMERIC_IS_EXPR_ENTRY (expr_entry));
	g_return_if_fail (scg == NULL ||IS_SHEET_CONTROL_GUI (scg));

	if ((expr_entry->flags & GNUM_EE_SINGLE_RANGE) ||
	    scg != expr_entry->scg)
		reset_rangesel (expr_entry);

	if (expr_entry->scg)
		gtk_signal_disconnect (GTK_OBJECT (expr_entry->scg),
				       expr_entry->id_cb_scg_destroy);

	if (scg) {
		expr_entry->id_cb_scg_destroy
			= gtk_signal_connect (
				GTK_OBJECT (scg), "destroy",
				GTK_SIGNAL_FUNC (cb_scg_destroy), expr_entry);

		expr_entry->target_sheet = sc_sheet (SHEET_CONTROL (scg));
	} else
		expr_entry->target_sheet = NULL;

	expr_entry->scg = scg;
}

/**
 * gnumeric_expr_entry_set_rangesel_from_text
 * @expr_entry: a #GnumericExprEntry
 * @text:       a string
 * 
 * Sets the text of the entry, and removes saved information about earlier
 * range selections.
 **/
/* FIXME: Should this be made more symmetrical with
   gnumeric_expr_entry_set_rangesel_from_range?
   Should it parse the rangesel? */
void
gnumeric_expr_entry_set_rangesel_from_text (GnumericExprEntry *expr_entry,
					    char *text)
{
	g_return_if_fail (GNUMERIC_IS_EXPR_ENTRY (expr_entry));
	g_return_if_fail (text != NULL);
	/* We have nowhere to store the text while frozen. */
	g_return_if_fail (expr_entry->freeze_count == 0);

	reset_rangesel (expr_entry);

	gtk_entry_set_text (GTK_ENTRY (expr_entry), text);
	expr_entry->rangesel.text_end = strlen (text);
}

static void
make_display_range (GnumericExprEntry *expr_entry, Range *dst)
{
	*dst = expr_entry->rangesel.range;
	
	if (expr_entry->flags & GNUM_EE_FULL_COL) {
		dst->start.row = 0;
		dst->end.row   = SHEET_MAX_ROWS - 1;
	} 
	if (expr_entry->flags & GNUM_EE_FULL_ROW) {
		dst->start.col = 0;
		dst->end.col   = SHEET_MAX_COLS - 1;
	} 
}

static char *
make_rangesel_text (GnumericExprEntry *expr_entry)
{
	char *buffer;
	gboolean inter_sheet;
	Range display_range;
	Range const *m;
	Rangesel const *rs = &expr_entry->rangesel;

	inter_sheet = (rs->sheet != expr_entry->target_sheet);
	make_display_range (expr_entry, &display_range);
	buffer = g_strdup_printf (
		"%s%s%s%d",
		(expr_entry->flags & GNUM_EE_ABS_COL) ? "$" : "",
		col_name (display_range.start.col),
		(expr_entry->flags & GNUM_EE_ABS_ROW) ? "$" : "",
		display_range.start.row+1);

	m = sheet_merge_is_corner (rs->sheet, &display_range.start);
	if (!range_is_singleton (&display_range) &&
	    ((m == NULL) || !range_equal (m, &display_range))) {
		char *tmp = g_strdup_printf (
			"%s:%s%s%s%d",buffer,
			(expr_entry->flags & GNUM_EE_ABS_COL) ? "$": "",
			col_name (display_range.end.col),
			(expr_entry->flags & GNUM_EE_ABS_ROW) ? "$": "",
			display_range.end.row+1);
		g_free (buffer);
		buffer = tmp;
	}
	if (inter_sheet || !(expr_entry->flags & GNUM_EE_SHEET_OPTIONAL)) {
		char *tmp = g_strdup_printf ("%s!%s", rs->sheet->name_quoted,
					     buffer);
		g_free (buffer);
		buffer = tmp;
	}
	
	return buffer;
}

static void
update_rangesel_text (GnumericExprEntry *expr_entry, char *text)
{
	GtkEditable *editable = GTK_EDITABLE (expr_entry);
	Rangesel *rs = &expr_entry->rangesel;
	int len;
	
	if (rs->text_end > rs->text_start) {
		gtk_editable_delete_text (editable,
					  rs->text_start,
					  rs->text_end);
		expr_entry->pos = rs->text_start;
		rs->text_end = rs->text_start;
	} else 
		rs->text_start = rs->text_end = expr_entry->pos;

	gtk_editable_set_position (GTK_EDITABLE (expr_entry), expr_entry->pos);

	if (!text)
		return;

	len = strlen (text);

	gtk_editable_insert_text (editable, text, len, &rs->text_end);

	/* Set the cursor at the end.  It looks nicer */
	gtk_editable_set_position (editable, rs->text_end);
}

/**
 * gnumeric_expr_entry_set_rangesel_from_range
 * @expr_entry: a #GnumericExprEntry
 * @r:          a #Range
 * @sheet:      a #sheet
 * @pos:        position
 *
 * Returns: true if displayed range is different from input range. false
 * otherwise.
 *
 * Sets the range selection and displays it in the entry text. If the widget
 * already contains a range selection, the new text replaces the
 * old. Otherwise, it is inserted at @pos.
 **/
gboolean
gnumeric_expr_entry_set_rangesel_from_range (GnumericExprEntry *expr_entry,
					     Range const *r, Sheet *sheet, int pos)
{
	char *rangesel_text;
	Rangesel *rs;
	gboolean needs_change = FALSE;
	
	g_return_val_if_fail (GNUMERIC_IS_EXPR_ENTRY (expr_entry), FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);
	g_return_val_if_fail (pos >= 0, FALSE);

	if (r)
		needs_change =  (expr_entry->flags & GNUM_EE_FULL_COL &&
 				 !range_is_full (r, TRUE)) ||
 				(expr_entry->flags & GNUM_EE_FULL_ROW &&
 				 !range_is_full (r, FALSE));

	rs = &expr_entry->rangesel;
	if (range_equal (r, &rs->range) && rs->sheet == sheet)
		return needs_change; /* FIXME ??? */

	if (r) 
		rs->range = *r;
	else
		memset (&rs->range, 0, sizeof (Range));
	
	rs->sheet = sheet;
	expr_entry->pos = pos;

	if (expr_entry->freeze_count == 0) {
		rangesel_text = make_rangesel_text (expr_entry);
		
		update_rangesel_text (expr_entry, rangesel_text);
		
		g_free (rangesel_text);
	}
	return needs_change;
}

/**
 * gnumeric_expr_entry_get_rangesel
 * @expr_entry: a #GnumericExprEntry
 * @r:          address to receive #Range
 * @sheet:      address to receive #sheet
 *
 * Get the range selection. Range is copied, Sheet is not. If sheet
 * argument is NULL, the corresponding value is not returned.
 **/
void
gnumeric_expr_entry_get_rangesel (GnumericExprEntry *expr_entry,
				  Range *r, Sheet **sheet)
{
	g_return_if_fail (GNUMERIC_IS_EXPR_ENTRY (expr_entry));
	g_return_if_fail (r != NULL);

	if (r)
		make_display_range (expr_entry, r);
	if (sheet)
		*sheet = expr_entry->rangesel.sheet;
}

/**
 * gnumeric_expr_entry_rangesel_stopped
 * @expr_entry:   a #GnumericExprEntry
 * @clear_string: clear string flag
 * 
 * Perform the appropriate action when a range selection has been completed.
 **/
void
gnumeric_expr_entry_rangesel_stopped (GnumericExprEntry *expr_entry,
				      gboolean clear_string)
{
	Rangesel *rs;

	g_return_if_fail (GNUMERIC_IS_EXPR_ENTRY (expr_entry));

	rs = &expr_entry->rangesel;
	if (clear_string && rs->text_end > rs->text_start)
		gtk_editable_delete_text (GTK_EDITABLE (expr_entry),
					  rs->text_start, rs->text_end);

	if (!(expr_entry->flags & GNUM_EE_SINGLE_RANGE) || clear_string)
		reset_rangesel (expr_entry);
}

/**
 * gnumeric_expr_entry_set_absolute
 * @expr_entry:   a #GnumericExprEntry
 * 
 * Select absolute reference mode for rows and columns. Do not change
 * displayed text. This is a convenience function which wraps
 * gnumeric_expr_entry_set_flags.
 **/
void
gnumeric_expr_entry_set_absolute (GnumericExprEntry *expr_entry)
{
	GnumericExprEntryFlags flags, mask;

	flags = GNUM_EE_ABS_ROW | GNUM_EE_ABS_COL;
	mask  = flags;
	gnumeric_expr_entry_set_flags (expr_entry, flags, mask);
}

/**
 * gnumeric_expr_entry_toggle_absolute
 * @expr_entry:   a #GnumericExprEntry
 * 
 * Cycle absolute reference mode through the sequence rel/rel, abs/abs,
 * rel/abs, abs/rel and back to rel/rel. Update text displayed in entry.
 **/
void
gnumeric_expr_entry_toggle_absolute (GnumericExprEntry *expr_entry)
{
	Rangesel *rs;
	gboolean abs_row, abs_col;
	GnumericExprEntryFlags mask = GNUM_EE_ABS_ROW | GNUM_EE_ABS_COL;
	GnumericExprEntryFlags flags = 0;

	g_return_if_fail (GNUMERIC_IS_EXPR_ENTRY (expr_entry));

	rs = &expr_entry->rangesel;
	
	/* It's late. I'm doing this the straightforward way. */
	abs_row = (expr_entry->flags & GNUM_EE_ABS_ROW) != 0;
	abs_col = (expr_entry->flags & GNUM_EE_ABS_COL) != 0;
	abs_row = (abs_row == abs_col);
	abs_col = !abs_col;
	if (abs_row)
		flags |= GNUM_EE_ABS_ROW;
	if (abs_col)
		flags |= GNUM_EE_ABS_COL;
	gnumeric_expr_entry_set_flags (expr_entry, flags, mask);
	if (rs->text_start < rs->text_end) {
		char *rangesel_text;

		expr_entry->pos = 0;
		rangesel_text = make_rangesel_text (expr_entry);
		update_rangesel_text (expr_entry, rangesel_text);
		g_free (rangesel_text);
	}
}

/**
 * gnumeric_expr_entry_rangesel_meaningful
 * @expr_entry:   a #GnumericExprEntry
 * 
 * Returns TRUE if a range selection is meaningful at current position.
 * eg it isn't at '=sum', or 'bob', but it is at '=sum('.
 *
 * NOTE:
 * Removed ')' and ':' from the set of characters where a range selection
 * may start. This is to fix bug report 54828:
 * "1) Start Gnumeric
 *  2) Navigate to cell C5
 *  3) Enter something like "=SUOM(A1:A10)"
 *  4) Try to move backwards with the left arrow key (which normally takes
 *     you backwards through the text you entered)
 *  for some reason you start navigating on the sheet with the
 *  rangesel cursor. I think it's sensible to start rangesel mode if we had
 * typed "=SUOM(", But in this case there is no reason at all to jump into
 * rangesel mode because the expression is closed/finished."
 * 2000-05-22 Jon Kåre Hellan <hellan@acm.org>
 **/
gboolean
gnumeric_expr_entry_rangesel_meaningful (GnumericExprEntry *entry)
{
	int cursor_pos;

	g_return_val_if_fail (entry != NULL, FALSE);

	cursor_pos = GTK_EDITABLE (entry)->current_pos;

	if (NULL == gnumeric_char_start_expr_p (GTK_ENTRY (entry)->text_mb) ||
	    cursor_pos <= 0)
		return FALSE;

	switch (GTK_ENTRY (entry)->text [cursor_pos-1]){
	case ',': case '=':
	case '(': case '<': case '>':
	case '+': case '-': case '*': case '/':
	case '^': case '&': case '%': case '!':
		return TRUE;

	default :
		return FALSE;
	};
}

/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * gnumeric-expr-entry.c: An entry widget specialized to handle expressions
 * and ranges. 
 *
 * Author:
 *   Jon Kåre Hellan (hellan@acm.org)
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include "gnumeric-expr-entry.h"

#include <workbook-edit.h>
#include <workbook-control-gui-priv.h>
#include <sheet-control-gui.h>
#include <sheet-merge.h>
#include <parse-util.h>
#include <gui-util.h>
#include <ranges.h>
#include <value.h>
#include <expr.h>
#include <eval.h>
#include <sheet.h>
#include <commands.h>

#include <gal/util/e-util.h>
#include <gtk/gtkentry.h>
#include <ctype.h>
#include <stdio.h>

static GtkObjectClass *gnumeric_expr_entry_parent_class;

typedef struct {
	Range range;
	Sheet *sheet;
	int text_start;
	int text_end;
	gboolean  abs_col;
	gboolean  abs_row;
} Rangesel;

struct _GnumericExprEntry {
	GtkEntry entry;
	WorkbookControlGUI *wbcg;
	SheetControlGUI *scg;
	GnumericExprEntryFlags flags;
	int freeze_count;
	Sheet *target_sheet;
	Rangesel rangesel;
};

typedef struct _GnumericExprEntryClass {
	GtkEntryClass parent_class;
	void (* rangesel_drag_finished) (GnumericExprEntry *gee);
} GnumericExprEntryClass;

/* Signals */
enum {
	RANGESEL_DRAG_FINISHED,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };

static void
reset_rangesel (GnumericExprEntry *ee)
{
	Rangesel *rs = &ee->rangesel;
	
	rs->sheet = NULL;
	rs->text_start = 0;
	rs->text_end = 0;
	memset (&rs->range, 0, sizeof (Range));

	/* restore the default based on the flags */
	ee->rangesel.abs_col = (ee->flags & GNUM_EE_ABS_COL) != 0;
	ee->rangesel.abs_row = (ee->flags & GNUM_EE_ABS_ROW) != 0;
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
		rs->abs_col ? "$" : "",
		col_name (display_range.start.col),
		rs->abs_row ? "$" : "",
		display_range.start.row+1);

	m = sheet_merge_is_corner (rs->sheet, &display_range.start);
	if (!range_is_singleton (&display_range) &&
	    ((m == NULL) || !range_equal (m, &display_range))) {
		char *tmp = g_strdup_printf (
			"%s:%s%s%s%d",buffer,
			rs->abs_col ? "$" : "",
			col_name (display_range.end.col),
			rs->abs_row ? "$" : "",
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
update_rangesel_text (GnumericExprEntry *expr_entry)
{
	GtkEditable *editable = GTK_EDITABLE (expr_entry);
	Rangesel *rs = &expr_entry->rangesel;
	int len;
	
	char *text = make_rangesel_text (expr_entry);
	if (rs->text_end > rs->text_start) {
		gtk_editable_delete_text (editable,
					  rs->text_start,
					  rs->text_end);
		rs->text_end = rs->text_start;
		gtk_editable_set_position (GTK_EDITABLE (expr_entry), rs->text_end);
	} else 
		rs->text_start = rs->text_end =
			gtk_editable_get_position (GTK_EDITABLE (expr_entry));

	if (text == NULL)
		return;

	/* Set the cursor at the end.  It looks nicer */
	len = strlen (text);
	gtk_editable_insert_text (editable, text, len, &rs->text_end);
	gtk_editable_set_position (editable, rs->text_end);
	g_free (text);
}

static gboolean
split_char_p (unsigned char c)
{
	switch (c) {
	case ',': case '=':
	case '(': case '<': case '>':
	case '+': case '-': case '*': case '/':
	case '^': case '&': case '%': case '!':
		return TRUE;
	default :
		return FALSE;
	}
}

/**
 * gnumeric_expr_entry_rangesel_start
 * @expr_entry:   a #GnumericExprEntry
 * 
 * Look at the current selection to see how much of it needs to be changed when
 * selecting a range.
 *
 **/
void
gnumeric_expr_entry_rangesel_start (GnumericExprEntry *ee)
{
	int cursor, start, last, end;
	char const *text;
	char *test;
	Rangesel *rs;
	gboolean single = (ee->flags & (GNUM_EE_SINGLE_RANGE != 0));
 
	rs = &ee->rangesel;
	text = gtk_entry_get_text (GTK_ENTRY (ee));
	cursor = gtk_editable_get_position (GTK_EDITABLE (ee));
	last = (text == NULL) ? 0 : strlen (text);
	rs->abs_col = FALSE;
	rs->abs_row = FALSE;
	rs->sheet = ee->target_sheet;
	if (text == NULL)
		return;
	
	for (start = 0; start <= cursor; start++) {
		for (end = last; end >= cursor; end--) {
			GSList *ranges;

			test = g_strndup (text + start, end - start);
			ranges = global_range_list_parse (ee->target_sheet, test);
			g_free(test);

			if (ranges != NULL) {
				if ((ranges->next == NULL) || single) {
				       /* Note: 
					* If single is not true, we just have one range here!
					**/
					Value *value;
					value = (Value *) ((g_slist_last (ranges))->data);
					rs->abs_col = !value->v_range.cell.a.col_relative;
					rs->abs_row = !value->v_range.cell.a.row_relative;
					rs->text_start = start;
					rs->text_end = end;
					if (single) {
						rs->text_start = 0;
						rs->text_end = last;
					}
					range_list_destroy (ranges);
					return;
				}
				range_list_destroy (ranges);
			}
		}
	}
	if (single) {
		rs->text_start = 0;
		rs->text_end = last;
	} else {
		for (start = cursor; start >= 0; start--) {
			if (!isalnum (*((unsigned char *)(text + start)))) {
				break;
			}
		}
		start++;
		rs->text_start = (cursor < start) ? cursor : start;
		for (end = cursor; end < last; end++) {
			if (!isalnum (*((unsigned char *)(text + end)))) {
				break;
			}
		}
		rs->text_end = (cursor < end) ? end : cursor;
	}
	return;
}

/**
 * gnumeric_expr_entry_rangesel_stop
 * @expr_entry:   a #GnumericExprEntry
 * @clear_string: clear string flag
 * 
 * Perform the appropriate action when a range selection has been completed.
 **/
void
gnumeric_expr_entry_rangesel_stop (GnumericExprEntry *expr_entry,
				   gboolean clear_string)
{
	Rangesel *rs;

	g_return_if_fail (IS_GNUMERIC_EXPR_ENTRY (expr_entry));

	rs = &expr_entry->rangesel;
	if (clear_string && rs->text_end > rs->text_start)
		gtk_editable_delete_text (GTK_EDITABLE (expr_entry),
					  rs->text_start, rs->text_end);

	if (!(expr_entry->flags & GNUM_EE_SINGLE_RANGE) || clear_string)
		reset_rangesel (expr_entry);
}

/***************************************************************************/

static void
cb_scg_destroy (GnumericExprEntry *gee, SheetControlGUI *scg)
{
	g_return_if_fail (scg == gee->scg);

	reset_rangesel (gee);
	gee->scg = NULL;
	gee->target_sheet = NULL;
}
	
static void
gee_detach_scg (GnumericExprEntry *gee)
{
	if (gee->scg != NULL) {
		  g_object_weak_unref (G_OBJECT (gee->scg),
			(GWeakNotify) cb_scg_destroy, gee);
		  gee->scg = NULL;
	}
}

/**
 * gnumeric_expr_entry_end_of_drag:
 * @gee :
 *
 * Signal the expression entry clients that a drag selection has just finished.
 * This is useful for knowing when to attempt to parse and update things that
 * depend on the modified expression without doing it in real time.
 **/
void
gnumeric_expr_entry_end_of_drag	(GnumericExprEntry *gee)
{
	gtk_signal_emit (GTK_OBJECT (gee), signals [RANGESEL_DRAG_FINISHED]);
}

/***************************************************************************/

static void
gnumeric_expr_entry_destroy (GtkObject *object)
{
	gee_detach_scg (GNUMERIC_EXPR_ENTRY (object));
	GTK_OBJECT_CLASS (gnumeric_expr_entry_parent_class)->destroy (object);
}

static gint
gnumeric_expr_entry_key_press_event (GtkWidget *widget, GdkEventKey *event)
{
	GnumericExprEntry *gee = GNUMERIC_EXPR_ENTRY (widget);
	WorkbookControlGUI *wbcg  =  gee->wbcg;
	GtkEntry           *entry = &gee->entry;
	int state = gnumeric_filter_modifiers (event->state);
	gint result;

	switch (event->keyval) {
	case GDK_Up:	case GDK_KP_Up:
	case GDK_Down:	case GDK_KP_Down:
		/* Ignore these keys */
		return TRUE;

	case GDK_F4: {
		/* Cycle absolute reference mode through the sequence rel/rel,
		 * abs/abs, rel/abs, abs/rel and back to rel/rel. Update text
		 * displayed in entry.
		 */
		Rangesel *rs = &gee->rangesel;
		gboolean abs_cols = (gee->flags & GNUM_EE_ABS_COL);
		gboolean abs_rows = (gee->flags & GNUM_EE_ABS_ROW);

		/* Look for a range */
		if (rs->text_start >= rs->text_end)
			gnumeric_expr_entry_rangesel_start (gee);

		/* no range found */
		if (rs->text_start >= rs->text_end)
			return TRUE;

		/* rows must be absolute */
		if (abs_rows) {
			if (abs_cols)
				return TRUE;
			rs->abs_col = !rs->abs_col;
		} else if (abs_cols)
			rs->abs_row = !rs->abs_row;
		else {
			/* It's late. I'm doing this the straightforward way. */
			rs->abs_row = (rs->abs_row == rs->abs_col);
			rs->abs_col = !rs->abs_col;
		}

		update_rangesel_text (gee);

		return TRUE;
	}

	case GDK_Escape:
		wbcg_edit_finish (wbcg, FALSE);
		return TRUE;

	case GDK_KP_Enter:
	case GDK_Return:
		/* Is this the right way to append a newline ?? */
		if (state == GDK_MOD1_MASK) {
			gtk_entry_append_text (entry, "\n");
			return TRUE;
		}

		/* Ctrl-enter is only applicable for the main entry */
		if (!wbcg_is_editing (wbcg))
			break;

		if (state == GDK_CONTROL_MASK ||
		    state == (GDK_CONTROL_MASK|GDK_SHIFT_MASK)) {
			ParsePos pos;
			gboolean const is_array = (state & GDK_SHIFT_MASK);
			char const *text = gtk_entry_get_text (
				GTK_ENTRY (wbcg_get_entry (wbcg)));
			Sheet *sheet = wbcg->editing_sheet;

			/* Be careful to use the editing sheet */
			cmd_area_set_text (WORKBOOK_CONTROL (wbcg),
				parse_pos_init (&pos, NULL, sheet,
					sheet->edit_pos.col, sheet->edit_pos.row),
				text, is_array);

			/* Finish editing but do NOT store the results
			 * If the assignment was successful it will
			 * have taken care of that.
			 */
			wbcg_edit_finish (wbcg, FALSE);
			return TRUE;
		}

	default:
		break;
	}

	result = GTK_WIDGET_CLASS (gnumeric_expr_entry_parent_class)->key_press_event (widget, event);

	if (!gnumeric_expr_entry_rangesel_meaningful (gee))
		scg_rangesel_stop (gee->scg, FALSE);

	return result;
}

static int
gnumeric_expr_entry_button_press_event (GtkWidget *widget, GdkEventButton *event)
{
	int result;
	result = GTK_WIDGET_CLASS (gnumeric_expr_entry_parent_class)->
		button_press_event (widget, event);

	if (!gnumeric_expr_entry_rangesel_meaningful (GNUMERIC_EXPR_ENTRY (widget)))
		scg_rangesel_stop (GNUMERIC_EXPR_ENTRY (widget)->scg, FALSE);

	return result;
}

static void
gnumeric_expr_entry_class_init (GtkObjectClass *object_class)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (object_class);

	gnumeric_expr_entry_parent_class
		= gtk_type_class (gtk_entry_get_type());

	object_class->destroy		= gnumeric_expr_entry_destroy;
	widget_class->key_press_event   = gnumeric_expr_entry_key_press_event;

/*
 *      FIXME:
 *      You would think that rather than button_press_event we would use a signal
 *      specific to the movement of the cursor (that signal would probably be at the 
 *      GTKENTRY level). Unfortunately there is no such signal, yet. In Gnome2
 *      GTKENTRY will have a move_cursor signal which we should use then!
 *
 */

	widget_class->button_press_event= gnumeric_expr_entry_button_press_event;

	signals [RANGESEL_DRAG_FINISHED] =
		gtk_signal_new (
			"rangesel_drag_finished",
			GTK_RUN_LAST,
			GTK_CLASS_TYPE (object_class),
			GTK_SIGNAL_OFFSET (GnumericExprEntryClass, rangesel_drag_finished),
			gtk_marshal_NONE__NONE,
			GTK_TYPE_NONE, 0);
}

E_MAKE_TYPE (gnumeric_expr_entry, "GnumericExprEntry", GnumericExprEntry,
	     gnumeric_expr_entry_class_init, NULL,
	     GTK_TYPE_ENTRY)

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
gnumeric_expr_entry_new (WorkbookControlGUI *wbcg)
{
	GnumericExprEntry *expr_entry;

	expr_entry = gtk_type_new (gnumeric_expr_entry_get_type ());

	expr_entry->flags |= GNUM_EE_SINGLE_RANGE;
	expr_entry->wbcg = wbcg;

	return GTK_WIDGET (expr_entry);
}

void
gnumeric_expr_entry_freeze (GnumericExprEntry *expr_entry)
{
	g_return_if_fail (expr_entry != NULL);
	g_return_if_fail (IS_GNUMERIC_EXPR_ENTRY (expr_entry));
			  
	expr_entry->freeze_count++;
}

void
gnumeric_expr_entry_thaw (GnumericExprEntry *expr_entry)
{
	g_return_if_fail (expr_entry != NULL);
	g_return_if_fail (IS_GNUMERIC_EXPR_ENTRY (expr_entry));

	if (expr_entry->freeze_count > 0 &&
	    (--expr_entry->freeze_count) == 0)
		update_rangesel_text (expr_entry);
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
 * %GNUM_EE_ABS_COL           Column reference must be absolute.
 * %GNUM_EE_ABS_ROW           Row reference must be absolute.
 * %GNUM_EE_FULL_COL          Range consists of full columns.
 * %GNUM_EE_FULL_ROW          Range consists of full rows.
 * %GNUM_EE_SHEET_OPTIONAL    Current sheet name not auto-added.
 **/
void
gnumeric_expr_entry_set_flags (GnumericExprEntry *ee,
			       GnumericExprEntryFlags flags,
			       GnumericExprEntryFlags mask)
{
	g_return_if_fail (IS_GNUMERIC_EXPR_ENTRY (ee));

	ee->flags = (ee->flags & ~mask) | (flags & mask);
	if (mask & GNUM_EE_ABS_COL)
		ee->rangesel.abs_col = (ee->flags & GNUM_EE_ABS_COL) != 0;
	if (mask & GNUM_EE_ABS_ROW)
		ee->rangesel.abs_row = (ee->flags & GNUM_EE_ABS_ROW) != 0;
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
gnumeric_expr_entry_set_scg (GnumericExprEntry *gee,
			     SheetControlGUI *scg)
{
	g_return_if_fail (IS_GNUMERIC_EXPR_ENTRY (gee));
	g_return_if_fail (scg == NULL || IS_SHEET_CONTROL_GUI (scg));

	if ((gee->flags & GNUM_EE_SINGLE_RANGE) || scg != gee->scg)
		reset_rangesel (gee);

	gee_detach_scg (gee);
	gee->scg = scg;
	if (scg) {
		g_object_weak_ref (G_OBJECT (gee->scg),
				   (GWeakNotify) cb_scg_destroy, gee);
		gee->target_sheet = sc_sheet (SHEET_CONTROL (scg));
	} else
		gee->target_sheet = NULL;
}

/**
 * gnumeric_expr_entry_clear :
 * @gee : The expr_entry
 *
 * Clear flags and entry.
 */
void
gnumeric_expr_entry_clear (GnumericExprEntry *expr_entry)
{
	g_return_if_fail (IS_GNUMERIC_EXPR_ENTRY (expr_entry));
	/* We have nowhere to store the text while frozen. */
	g_return_if_fail (expr_entry->freeze_count == 0);

	reset_rangesel (expr_entry);
	gtk_entry_set_text (GTK_ENTRY (expr_entry), "");
}

/**
 * gnumeric_expr_entry_set_rangesel_from_dep
 * @expr_entry: a #GnumericExprEntry
 * @dep: A dependent
 * 
 * Sets the text of the entry, and removes saved information about earlier
 * range selections.
 **/
void
gnumeric_expr_entry_set_rangesel_from_dep (GnumericExprEntry *expr_entry,
					   Dependent const *dep)
{
	g_return_if_fail (IS_GNUMERIC_EXPR_ENTRY (expr_entry));
	g_return_if_fail (dep != NULL);
	/* We have nowhere to store the text while frozen. */
	g_return_if_fail (expr_entry->freeze_count == 0);

	if (dep->expression != NULL) {
		ParsePos pp;
		char *text = expr_tree_as_string (dep->expression,
			parse_pos_init_dep (&pp, dep));

		reset_rangesel (expr_entry);
		gtk_entry_set_text (GTK_ENTRY (expr_entry), text);
		expr_entry->rangesel.text_end = strlen (text);
		g_free (text);
	} else
		gnumeric_expr_entry_clear (expr_entry);
}

/**
 * gnumeric_expr_entry_set_range
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
gnumeric_expr_entry_set_range (GnumericExprEntry *expr_entry,
			       Sheet *sheet, Range const *r)
{
	Rangesel *rs;
	gboolean needs_change = FALSE;
	
	g_return_val_if_fail (IS_GNUMERIC_EXPR_ENTRY (expr_entry), FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);

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

	if (expr_entry->freeze_count == 0)
		update_rangesel_text (expr_entry);

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
	g_return_if_fail (IS_GNUMERIC_EXPR_ENTRY (expr_entry));
	g_return_if_fail (r != NULL);

	if (r)
		make_display_range (expr_entry, r);
	if (sheet)
		*sheet = expr_entry->rangesel.sheet;
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
	GnumericExprEntryFlags flags;

	flags = GNUM_EE_ABS_ROW | GNUM_EE_ABS_COL;
	gnumeric_expr_entry_set_flags (expr_entry, flags, flags);
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
gnumeric_expr_entry_rangesel_meaningful (GnumericExprEntry *ee)
{
	int cursor_pos;
	char const *text;

	g_return_val_if_fail (IS_GNUMERIC_EXPR_ENTRY (ee), FALSE);

	text = gtk_entry_get_text (GTK_ENTRY (ee));

	/* We need to be editing an expression */
	if (!wbcg_edit_has_guru (ee->wbcg) &&
	    gnumeric_char_start_expr_p (text) == NULL)
		return FALSE;

	gnumeric_expr_entry_rangesel_start (ee);
	if (ee->rangesel.text_end != ee->rangesel.text_start)
		return TRUE;

	cursor_pos = gtk_editable_get_position (GTK_EDITABLE (ee));
	return (cursor_pos <= 0) || split_char_p (text [cursor_pos-1]);
}

/**
 * gnumeric_expr_entry_parse :
 * @ee : the entry
 * @pp : a parse position
 * @start_sel : start range selection when things change.
 *
 * Attempts to parse the content of the entry line honouring
 * the flags.
 */
ExprTree *
gnumeric_expr_entry_parse (GnumericExprEntry *ee, ParsePos const *pp,
			   gboolean start_sel)
{
	char const *text;
	char *str;
	ExprTree *expr;
	int flags;

	g_return_val_if_fail (IS_GNUMERIC_EXPR_ENTRY (ee), NULL);

	text = gtk_entry_get_text (GTK_ENTRY (ee));

	if (text == NULL || text[0] == '\0')
		return NULL;

	flags = GNM_PARSER_DEFAULT;
	if (ee->flags & GNUM_EE_ABS_COL)
		flags |= GNM_PARSER_FORCE_ABSOLUTE_COL_REFERENCES;
	if (ee->flags & GNUM_EE_ABS_ROW)
		flags |= GNM_PARSER_FORCE_ABSOLUTE_ROW_REFERENCES;
	if (!(ee->flags & GNUM_EE_SHEET_OPTIONAL))
		flags |= GNM_PARSER_FORCE_EXPLICIT_SHEET_REFERENCES;

	expr = expr_parse_str (text, pp, flags, NULL, NULL);
	if (expr == NULL)
		return NULL;

	if (ee->flags & GNUM_EE_SINGLE_RANGE) {
		Value *range = expr_tree_get_range (expr) ;
		if (range == NULL) {
			expr_tree_unref (expr);
			return NULL;
		}
		value_release (range);
	}

	/* Reset the entry in case something changed */
	str = expr_tree_as_string (expr, pp);
	if (strcmp (str, text)) {
		SheetControlGUI *scg = wb_control_gui_cur_sheet (ee->wbcg);
		if (start_sel && sc_sheet (SHEET_CONTROL (scg)) == ee->rangesel.sheet) {
			Range const *r = &ee->rangesel.range;
			scg_rangesel_bound (scg,
				r->start.col, r->start.row,
				r->end.col, r->end.row);
		} else
			gtk_entry_set_text (GTK_ENTRY (ee), str);
	}
	g_free (str);

	return expr;
}

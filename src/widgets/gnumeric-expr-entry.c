/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * gnumeric-expr-entry.c: An entry widget specialized to handle expressions
 * and ranges. 
 *
 * Author:
 *   Jon Kåre Hellan (hellan@acm.org)
 */

#include <config.h>
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
	guint id_cb_scg_destroy;
	Sheet *target_sheet;
	Rangesel rangesel;
};

typedef struct _GnumericExprEntryClass {
	GtkEntryClass parent_class;
} GnumericExprEntryClass;

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
 * NOTE : This routine is damn ugly.  I do not like it one bit.
 * There must be a cleaner way to do this.
 **/
void
gnumeric_expr_entry_rangesel_start (GnumericExprEntry *ee)
{
	gboolean singleton = TRUE, anal_safety = TRUE;
	int start;
	char const *text, *end;
	Rangesel *rs;
	CellRef ref1, ref2;
	CellPos pos;

	g_return_if_fail (IS_GNUMERIC_EXPR_ENTRY (ee));

	rs = &ee->rangesel;
	text = gtk_entry_get_text (GTK_ENTRY (ee));
	start = gtk_editable_get_position (GTK_EDITABLE (ee));
	rs->text_start = start;
	rs->text_end = start;

	if (text == NULL)
		return;

	/* This makes addresses use same coord system as absolute */
	pos.col = pos.row = 0;

	/* is the cursor in a sheet name ? Do a quick ugly search */
	end = text + start;
	while (end[0] != '\0' && !split_char_p (end[0]))
		end++;
	if (end[0] == '!')
		start = end - text + 1;

	loop :
		end = cellref_get (&ref1, text+start, &pos);
		if (end == NULL && start > 0 &&
		    ('$' == text [start-1] ||
		     isalnum (*((unsigned char *)(text + start -1))))) {
			start--;
			goto loop;
		}
	if (end == NULL)
		return;

	/* search the start of the reference, match $AA$1 rather than A$1 */
	for (; start > 0 ; start--) {
		CellRef tmp;
		char const *tmp_end = cellref_get (&tmp, text+start-1, &pos);
		if (tmp_end == NULL)
			break;
		end = tmp_end;
		ref1 = tmp;
	}

	/* This is the first cell */
	if (*end == ':') {
		char const *end2 = cellref_get (&ref2, end+1, &pos);
		singleton = (end2 != NULL);
		rs->text_end = (singleton ? end2 : end) - text;
		rs->text_start = start;
	} else if (start >= 3 && text [start-1] == ':' && anal_safety) {
		anal_safety = FALSE;
		start -= 3;
		goto loop;
	} else {
		rs->text_start = start;
		rs->text_end = end - text;
	}
	rs->abs_col = !ref1.col_relative;
	rs->abs_row = !ref1.row_relative;
	rs->range.start.col = ref1.col;
	rs->range.start.row = ref1.row;
	if (!singleton) {
		rs->range.end.col = ref2.col;
		rs->range.end.row = ref2.row;
	} else
		rs->range.end = rs->range.start;

	/* default to avoid crash on error */
	rs->sheet = ee->target_sheet;
	if (start >= 2 && text[start-1] == '!') {
		start -= 2;
		if (text[start] == '\'') {
			loop2:
			if (start >= 1 && text[start-1] != '\'') {
				start--;
				goto loop2;
			}
			if (start >= 2 && text[start-2] == '\\') {
				start -= 2;
				goto loop2;
			}
		/* TODO build up the unquoted name */
		} else {
			while (start > 0 &&
			       isalnum (*((unsigned char *)(text + start -1))))
				start--;
		}
		/* TODO lookup the sheet */
		rs->text_start = start;
	}
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
gnumeric_expr_entry_destroy (GtkObject *object)
{
	GnumericExprEntry *expr_entry = GNUMERIC_EXPR_ENTRY (object);

	if (expr_entry->scg)
		gtk_signal_disconnect (GTK_OBJECT (expr_entry->scg),
				       expr_entry->id_cb_scg_destroy);

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
		if (!wb_control_gui_is_editing (wbcg))
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

static void
gnumeric_expr_entry_class_init (GtkObjectClass *object_class)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (object_class);

	gnumeric_expr_entry_parent_class
		= gtk_type_class (gtk_entry_get_type());

	object_class->destroy		= gnumeric_expr_entry_destroy;
	widget_class->key_press_event   = gnumeric_expr_entry_key_press_event;
}

E_MAKE_TYPE (gnumeric_expr_entry, "GnumericExprEntry", GnumericExprEntry,
	     gnumeric_expr_entry_class_init, NULL,
	     GTK_TYPE_ENTRY);

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
 * %GNUM_EE_ABS_COL           Column reference must be absolute.
 * %GNUM_EE_ABS_ROW           Row reference must be absolute.
 * %GNUM_EE_FULL_COL          Range consists of full columns.
 * %GNUM_EE_FULL_ROW          Range consists of full rows.
 * %GNUM_EE_SHEET_OPTIONAL    Sheet must not be displayed if current sheet.
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
gnumeric_expr_entry_set_scg (GnumericExprEntry *expr_entry,
			     SheetControlGUI *scg)
{
	g_return_if_fail (IS_GNUMERIC_EXPR_ENTRY (expr_entry));
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
 *
 * Attempts to parse the content of the entry line honouring
 * the flags.
 */
ExprTree *
gnumeric_expr_entry_parse (GnumericExprEntry *ee, ParsePos const *pp)
{
	char const *text;
	char *str;
	ExprTree *expr;
	ParseError err;
	StyleFormat *desired_format;
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
	expr = gnumeric_expr_parser (text, pp,
		flags, &desired_format,
		parse_error_init (&err));

	/* FIXME : what to do with errors ? */
	parse_error_free (&err);

	if (expr == NULL)
		return expr;

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
	gtk_entry_set_text (GTK_ENTRY (ee), str);
	g_free (str);

	return expr;
}

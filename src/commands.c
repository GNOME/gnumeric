/* vim: set sw=8:
 * $Id$
 */

/*
 * commands.c: Handlers to undo & redo commands
 *
 * Copyright (C) 1999, 2000 Jody Goldberg (jgoldberg@home.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#include <config.h>
#include "gnumeric-type-util.h"
#include "gnumeric-util.h"
#include "commands.h"
#include "application.h"
#include "sheet.h"
#include "workbook.h"
#include "workbook-view.h"
#include "ranges.h"
#include "sort.h"
#include "parse-util.h"
#include "clipboard.h"
#include "selection.h"
#include "datetime.h"
#include "colrow.h"
#include "dialogs.h"
#include "border.h"
#include "rendered-value.h"
#include "dialogs/dialog-autocorrect.h"
#include "sheet-autofill.h"

/*
 * NOTE : This is a work in progress
 *
 * Feel free to lend a hand.  There are several distinct stages to
 * wrapping each command.
 *
 * 1) Find the appropriate place(s) in the catch all calls to activations
 * of this logical function.  Be careful.  This should only be called by
 * user initiated actions, not internal commands.
 *
 * 2) Copy the boiler plate code into place and implement the descriptor.
 *
 * 3) Implement the guts of the support functions.
 *
 * That way undo redo just become applications of the old or the new styles.
 *
 * Design thoughts :
 * 1) redo : this should be renamed 'exec' and should be the place that the
 *    the actual command executes.  This avoid duplicating the code for
 *    application and re-application.
 *
 * 2) The command objects are responsible for generating recalc and redraw
 *    events.  None of the internal utility routines should do so.  Those are
 *    expensive events and should only be done once per command to avoid
 *    duplicating work.
 *
 * FIXME: Filter the list of commands when a sheet is deleted.
 *
 * TODO : Add user preference for undo buffer size limit (# of commands ?)
 * TODO : Possibly clear lists on save.
 *
 * TODO : Reqs for selective undo
 * TODO : Add Repeat last command
 *
 * Future thoughts
 * - undoable preference setting ?  XL does not have this.  Do we want it ?
 */
/******************************************************************/

#define GNUMERIC_COMMAND_TYPE        (gnumeric_command_get_type ())
#define GNUMERIC_COMMAND(o)          (GTK_CHECK_CAST ((o), GNUMERIC_COMMAND_TYPE, GnumericCommand))
#define GNUMERIC_COMMAND_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), GNUMERIC_COMMAND_TYPE, GnumericCommandClass))
#define IS_GNUMERIC_COMMAND(o)       (GTK_CHECK_TYPE ((o), GNUMERIC_COMMAND_TYPE))
#define IS_GNUMERIC_COMMAND_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), GNUMERIC_COMMAND_TYPE))

typedef struct
{
	GtkObject parent;
	char const *cmd_descriptor;	/* A string to put in the menu */
} GnumericCommand;

typedef gboolean (* UndoCmd)(GnumericCommand *this, CommandContext *context);
typedef gboolean (* RedoCmd)(GnumericCommand *this, CommandContext *context);

typedef struct {
	GtkObjectClass parent_class;

	UndoCmd		undo_cmd;
	RedoCmd		redo_cmd;
} GnumericCommandClass;

static GNUMERIC_MAKE_TYPE(gnumeric_command, "GnumericCommand",
			  GnumericCommand, NULL, NULL,
			  gtk_object_get_type());

/* Store the real GtkObject dtor pointer */
static void (* gtk_object_dtor) (GtkObject *object) = NULL;

static void
gnumeric_command_destroy (GtkObject *obj)
{
	GnumericCommand *cmd = GNUMERIC_COMMAND (obj);

	g_return_if_fail (cmd != NULL);

	/* The const was to avoid accidental changes elsewhere */
	g_free ((gchar *)cmd->cmd_descriptor);

	/* Call the base class dtor */
	g_return_if_fail (gtk_object_dtor);
	(*gtk_object_dtor) (obj);
}

#define GNUMERIC_MAKE_COMMAND(type, func) \
static gboolean \
func ## _undo (GnumericCommand *me, CommandContext *context); \
static gboolean \
func ## _redo (GnumericCommand *me, CommandContext *context); \
static void \
func ## _destroy (GtkObject *object); \
static void \
func ## _class_init (GnumericCommandClass * const parent) \
{	\
	parent->undo_cmd = (UndoCmd)& func ## _undo;		\
	parent->redo_cmd = (RedoCmd)& func ## _redo;		\
	if (gtk_object_dtor == NULL)				\
		gtk_object_dtor = parent->parent_class.destroy;	\
	parent->parent_class.destroy = & func ## _destroy;	\
} \
static GNUMERIC_MAKE_TYPE_WITH_PARENT(func, #type, type, GnumericCommandClass, func ## _class_init, NULL, \
				      gnumeric_command_get_type())

/******************************************************************/

/**
 * get_menu_label : Utility routine to get the descriptor associated
 *     with a list of commands.
 *
 * @cmd_list : The command list to check.
 *
 * Returns : A static reference to a descriptor.  DO NOT free this.
 */
static gchar const *
get_menu_label (GSList *cmd_list)
{
	if (cmd_list != NULL) {
		GnumericCommand *cmd = GNUMERIC_COMMAND (cmd_list->data);
		return cmd->cmd_descriptor;
	}

	return NULL;
}

/**
 * undo_redo_menu_labels : Another utility to set the menus correctly.
 *
 * workbook : The book whose undo/redo queues we are modifying
 */
static void
undo_redo_menu_labels (Workbook *wb)
{
	workbook_view_set_undo_redo_state (wb,
					   get_menu_label (wb->undo_commands),
					   get_menu_label (wb->redo_commands));
}

/*
 * command_undo : Undo the last command executed.
 *
 * @context : The command context which issued the request.
 *            Any user level errors generated by undoing will be reported
 *            here.
 *
 * @wb : The workbook whose commands to undo.
 */
void
command_undo (CommandContext *context, Workbook *wb)
{
	GnumericCommand *cmd;
	GnumericCommandClass *klass;

	g_return_if_fail (wb != NULL);
	g_return_if_fail (wb->undo_commands != NULL);

	cmd = GNUMERIC_COMMAND (wb->undo_commands->data);
	g_return_if_fail (cmd != NULL);

	klass = GNUMERIC_COMMAND_CLASS(cmd->parent.klass);
	g_return_if_fail (klass != NULL);

	/* TRUE indicates a failure to undo.  Leave the command where it is */
	if (klass->undo_cmd (cmd, context))
		return;

	wb->undo_commands = g_slist_remove (wb->undo_commands,
					    wb->undo_commands->data);
	wb->redo_commands = g_slist_prepend (wb->redo_commands, cmd);

	workbook_view_pop_undo (wb);
	workbook_view_push_redo (wb, cmd->cmd_descriptor);
	undo_redo_menu_labels (wb);
	/* TODO : Should we mark the workbook as clean or pristine too */
}

/*
 * command_redo : Redo the last command that was undone.
 *
 * @context : The command context which issued the request.
 *            Any user level errors generated by redoing will be reported
 *            here.
 *
 * @wb : The workbook whose commands to redo.
 */
void
command_redo (CommandContext *context, Workbook *wb)
{
	GnumericCommand *cmd;
	GnumericCommandClass *klass;

	g_return_if_fail (wb);
	g_return_if_fail (wb->redo_commands);

	cmd = GNUMERIC_COMMAND (wb->redo_commands->data);
	g_return_if_fail (cmd != NULL);

	klass = GNUMERIC_COMMAND_CLASS(cmd->parent.klass);
	g_return_if_fail (klass != NULL);

	/* TRUE indicates a failure to redo.  Leave the command where it is */
	if (klass->redo_cmd (cmd, context))
		return;

	/* Remove the command from the undo list */
	wb->redo_commands = g_slist_remove (wb->redo_commands,
					    wb->redo_commands->data);
	wb->undo_commands = g_slist_prepend (wb->undo_commands, cmd);

	workbook_view_push_undo (wb, cmd->cmd_descriptor);
	workbook_view_pop_redo (wb);
	undo_redo_menu_labels (wb);
}

/*
 * command_list_release : utility routine to free the resources associated
 *    with a list of commands.
 *
 * @cmd_list : The set of commands to free.
 *
 * NOTE : remember to NULL the list when you are done.
 */
void
command_list_release (GSList *cmd_list)
{
	while (cmd_list != NULL) {
		GtkObject *cmd = GTK_OBJECT (cmd_list->data);

		g_return_if_fail (cmd != NULL);

		gtk_object_unref (cmd);
		cmd_list = g_slist_remove (cmd_list, cmd_list->data);
	}
}

/**
 * command_push_undo : An internal utility to tack a new command
 *    onto the undo list.
 *
 * @context : The context that issued the command.
 * @wb : The workbook the command operated on.
 * @cmd : The new command to add.
 *
 * returns : TRUE if there was an error.
 */
static gboolean
command_push_undo (CommandContext *context, Workbook *wb, GtkObject *obj)
{
	gboolean trouble;
	GnumericCommand *cmd;
	GnumericCommandClass *klass;

	g_return_val_if_fail (wb, TRUE);

	cmd = GNUMERIC_COMMAND (obj);
	g_return_val_if_fail (cmd != NULL, TRUE);

	klass = GNUMERIC_COMMAND_CLASS(cmd->parent.klass);
	g_return_val_if_fail (klass != NULL, TRUE);

	/* TRUE indicates a failure to do the command */
	trouble = klass->redo_cmd (cmd, context);

	if  (!trouble) {
		command_list_release (wb->redo_commands);
		wb->redo_commands = NULL;

		wb->undo_commands = g_slist_prepend (wb->undo_commands, cmd);

		workbook_view_push_undo (wb, cmd->cmd_descriptor);
		workbook_view_clear_redo (wb);
		undo_redo_menu_labels (wb);
	} else
		gtk_object_unref (obj);

	return trouble;
}

/******************************************************************/

#define CMD_SET_TEXT_TYPE        (cmd_set_text_get_type ())
#define CMD_SET_TEXT(o)          (GTK_CHECK_CAST ((o), CMD_SET_TEXT_TYPE, CmdSetText))

typedef struct
{
	GnumericCommand parent;

	EvalPos	 pos;
	gchar		*text;
} CmdSetText;

GNUMERIC_MAKE_COMMAND (CmdSetText, cmd_set_text);

static gboolean
cmd_set_text_undo (GnumericCommand *cmd, CommandContext *context)
{
	CmdSetText *me = CMD_SET_TEXT(cmd);
	Cell *cell;
	char *new_text;

	g_return_val_if_fail (me != NULL, TRUE);

	/* Get the cell */
	cell = sheet_cell_get (me->pos.sheet,
			       me->pos.eval.col,
			       me->pos.eval.row);

	/* Save the new value so we can redo */
	new_text = (cell == NULL || cell->value == NULL || cell->value->type == VALUE_EMPTY)
	    ? NULL : cell_get_entered_text (cell);

	/* Restore the old value if it was not empty */
	if (me->text != NULL) {
		if (cell == NULL)
			cell = sheet_cell_new (me->pos.sheet,
					       me->pos.eval.col,
					       me->pos.eval.row);
		sheet_cell_set_text (cell, me->text);
		g_free (me->text);
	} else if (cell != NULL)
		sheet_cell_remove (me->pos.sheet, cell, TRUE);

	me->text = new_text;

	sheet_set_dirty (me->pos.sheet, TRUE);
	workbook_recalc (me->pos.sheet->workbook);
	sheet_update (me->pos.sheet);

	return FALSE;
}

static gboolean
cmd_set_text_redo (GnumericCommand *cmd, CommandContext *context)
{
	/* Undo and redo are the same for this case */
	return cmd_set_text_undo (cmd, context);
}

static void
cmd_set_text_destroy (GtkObject *cmd)
{
	CmdSetText *me = CMD_SET_TEXT(cmd);
	if (me->text != NULL) {
		g_free (me->text);
		me->text = NULL;
	}
	gnumeric_command_destroy (cmd);
}

gboolean
cmd_set_text (CommandContext *context,
	      Sheet *sheet, CellPos const *pos,
	      const char *new_text)
{
	static int const max_descriptor_width = 15;

	GtkObject *obj;
	CmdSetText *me;
	gchar *pad = "";
	gchar *text, *corrected_text;
	Cell const *cell;

	g_return_val_if_fail (sheet != NULL, TRUE);
	g_return_val_if_fail (new_text != NULL, TRUE);

	/* Ensure that we are not splitting up an array */
	cell = sheet_cell_get (sheet, pos->col, pos->row);
	if (cell_is_partial_array (cell)) {
		gnumeric_error_splits_array (context, _("Set Text"));
		return TRUE;
	}

	/* From src/dialogs/dialog-autocorrect.c */
	corrected_text = autocorrect_tool (new_text);

	obj = gtk_type_new (CMD_SET_TEXT_TYPE);
	me = CMD_SET_TEXT (obj);

	/* Store the specs for the object */
	me->pos.sheet = sheet;
	me->pos.eval = *pos;
	me->text = corrected_text;

	/* Limit the size of the descriptor to something reasonable */
	if (strlen (corrected_text) > max_descriptor_width) {
		pad = "..."; /* length of 3 */
		text = g_strndup (corrected_text,
				  max_descriptor_width - 3);
	} else
		text = (gchar *) corrected_text;

	me->parent.cmd_descriptor =
		g_strdup_printf (_("Typing \"%s%s\" in %s"), text, pad,
				 cell_pos_name(pos));

	if (*pad)
		g_free (text);

	/* Register the command object */
	return command_push_undo (context, sheet->workbook, obj);
}

/******************************************************************/

#define CMD_AREA_SET_TEXT_TYPE        (cmd_area_set_text_get_type ())
#define CMD_AREA_SET_TEXT(o)          (GTK_CHECK_CAST ((o), CMD_AREA_SET_TEXT_TYPE, CmdAreaSetText))

typedef struct
{
	GnumericCommand parent;

	EvalPos	 pos;
	char 	*text; 
	gboolean as_array;
	GSList	*old_content;
	GSList	*selection;
} CmdAreaSetText;

GNUMERIC_MAKE_COMMAND (CmdAreaSetText, cmd_area_set_text);

static gboolean
cmd_area_set_text_undo (GnumericCommand *cmd, CommandContext *context)
{
	CmdAreaSetText *me = CMD_AREA_SET_TEXT (cmd);
	GSList *ranges;

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->selection != NULL, TRUE);
	g_return_val_if_fail (me->old_content != NULL, TRUE);

	for (ranges = me->selection; ranges != NULL ; ranges = ranges->next) {
		Range const * const r = ranges->data;
		CellRegion * c;
		PasteTarget pt;

		g_return_val_if_fail (me->old_content != NULL, TRUE);

		c = me->old_content->data;
		clipboard_paste_region (context,
					paste_target_init (&pt, me->pos.sheet, r, PASTE_FORMULAS),
					c);
		clipboard_release (c);
		me->old_content = g_slist_remove (me->old_content, c);
	}
	g_return_val_if_fail (me->old_content == NULL, TRUE);

	sheet_set_dirty (me->pos.sheet, TRUE);
	workbook_recalc (me->pos.sheet->workbook);
	sheet_update (me->pos.sheet);

	return FALSE;
}

static gboolean
cmd_area_set_text_redo (GnumericCommand *cmd, CommandContext *context)
{
	CmdAreaSetText *me = CMD_AREA_SET_TEXT (cmd);
	ExprTree *expr = NULL;
	GSList *l;
	char const *start;

	g_return_val_if_fail (me != NULL, TRUE);

	/* Check for array subdivision */
	if (selection_check_for_array (me->pos.sheet, me->selection)) {
		gnumeric_error_splits_array (context, _("Set Text"));
		return TRUE;
	}

	/*
	 * Only enter an array formula if
	 *   1) the text is a formula
	 *   2) It's entered as an array formula
	 *   3) There is only one 1 selection
	 */
	l = me->selection;
	start = gnumeric_char_start_expr_p (me->text);
	if (start != NULL && me->as_array && l != NULL && l->next == NULL) {
		char *error_string = NULL;
		ParsePos pp;
		expr = expr_parse_string (start,
		    parse_pos_init_evalpos (&pp, &me->pos),
		    NULL, &error_string);

		if (expr == NULL)
			return TRUE;
	}

	/* Everything is ok. Store previous contents and perform the operation */
	for (l = me->selection ; l != NULL ; l = l->next) {
		Range const * const r = l->data;
		me->old_content = g_slist_prepend (me->old_content,
			clipboard_copy_range (me->pos.sheet, r));

		/* If there is an expression then this was an array */
		if (expr != NULL) 
			cell_set_array_formula (me->pos.sheet,
						r->start.row, r->start.col,
						r->end.row, r->end.col,
						expr, TRUE);
		else
			sheet_range_set_text (&me->pos, r, me->text);
	}

	/*
	 * Now that things have been filled in and recalculated we can generate
	 * the spans.  Non expression cells need to be rendered.
	 * TODO : We could be smarter here.  Only the left and
	 * right columns can span,
	 * so there is no need to check the middles.
	 */
	for (l = me->selection ; l != NULL ; l = l->next) {
		Range const * const r = l->data;
		sheet_range_calc_spans (me->pos.sheet, *r, SPANCALC_RENDER);
	}

	sheet_set_dirty (me->pos.sheet, TRUE);
	workbook_recalc (me->pos.sheet->workbook);
	sheet_update (me->pos.sheet);

	return FALSE;
}
static void
cmd_area_set_text_destroy (GtkObject *cmd)
{
	CmdAreaSetText *me = CMD_AREA_SET_TEXT (cmd);

	g_free (me->text);

	if (me->old_content != NULL) {
		GSList *l;
		for (l = me->old_content ; l != NULL ; l = g_slist_remove (l, l->data))
			clipboard_release (l->data);
		me->old_content = NULL;
	}
	if (me->selection != NULL) {
		GSList *l;
		for (l = me->selection ; l != NULL ; l = g_slist_remove (l, l->data))
			g_free (l->data);
		me->selection = NULL;
	}

	gnumeric_command_destroy (cmd);
}

gboolean
cmd_area_set_text (CommandContext *context, EvalPos const *pos,
		   char const *new_text, gboolean as_array)
{
	static int const max_descriptor_width = 15;

	GtkObject *obj;
	CmdAreaSetText *me;
	gchar *text, *pad = "";

	obj = gtk_type_new (CMD_AREA_SET_TEXT_TYPE);
	me = CMD_AREA_SET_TEXT (obj);

	/* Store the specs for the object */
	me->pos         = *pos;
	me->text        = g_strdup (new_text);
	me->as_array    = as_array;
	me->selection   = selection_get_ranges (pos->sheet, FALSE /* No intersection */);
	me->old_content = NULL;

	if (strlen(new_text) > max_descriptor_width) {
		pad = "..."; /* length of 3 */
		text = g_strndup (new_text,
				  max_descriptor_width - 3);
	} else
		text = (gchar *) new_text;

	me->parent.cmd_descriptor =
	    g_strdup_printf (_("Typing \"%s%s\""), text, pad);

	if (*pad)
		g_free (text);

	/* Register the command object */
	return command_push_undo (context, pos->sheet->workbook, obj);
}

/******************************************************************/

#define CMD_INS_DEL_ROW_COL_TYPE        (cmd_ins_del_row_col_get_type ())
#define CMD_INS_DEL_ROW_COL(o)          (GTK_CHECK_CAST ((o), CMD_INS_DEL_ROW_COL_TYPE, CmdInsDelRowCol))

typedef struct
{
	GnumericCommand parent;

	Sheet		*sheet;
	gboolean	 is_insert;
	gboolean	 is_cols;
	int		 index;
	int		 count;

	double		*sizes;
	CellRegion 	*contents;
	GSList		*reloc_storage;
} CmdInsDelRowCol;

GNUMERIC_MAKE_COMMAND (CmdInsDelRowCol, cmd_ins_del_row_col);

static gboolean
cmd_ins_del_row_col_undo (GnumericCommand *cmd, CommandContext *context)
{
	CmdInsDelRowCol *me = CMD_INS_DEL_ROW_COL(cmd);
	int index;
	GSList *tmp = NULL;
	gboolean trouble;
	Range r;
	PasteTarget pt;

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->sizes != NULL, TRUE);
	g_return_val_if_fail (me->contents != NULL, TRUE);

	if (!me->is_insert) {
		index = me->index;
		if (me->is_cols)
			trouble = sheet_insert_cols (context, me->sheet, me->index, me->count, &tmp);
		else
			trouble = sheet_insert_rows (context, me->sheet, me->index, me->count, &tmp);
	} else {
		index = ((me->is_cols) ? SHEET_MAX_COLS : SHEET_MAX_ROWS) - me->count;
		if (me->is_cols)
			trouble = sheet_delete_cols (context, me->sheet, me->index, me->count, &tmp);
		else
			trouble = sheet_delete_rows (context, me->sheet, me->index, me->count, &tmp);
	}

	/* restore row/col sizes */
	sheet_restore_row_col_sizes (me->sheet, me->is_cols, index, me->count,
				     me->sizes);
	me->sizes = NULL;

	/* restore row/col contents */
	if (me->is_cols)
		range_init (&r, index, 0, index, SHEET_MAX_ROWS-1);
	else
		range_init (&r, 0, index, SHEET_MAX_COLS-1, index);

	clipboard_paste_region (context,
				paste_target_init (&pt, me->sheet, &r, PASTE_ALL_TYPES),
				me->contents);
	clipboard_release (me->contents);
	me->contents = NULL;

	/* Throw away the undo info for the expressions after the action*/
	workbook_expr_unrelocate_free (tmp);

	/* Restore the changed expressions before the action */
	workbook_expr_unrelocate (me->sheet->workbook, me->reloc_storage);
	me->reloc_storage = NULL;

	sheet_set_dirty (me->sheet, TRUE);
	workbook_recalc (me->sheet->workbook);
	sheet_update (me->sheet);

	/* Ins/Del Row/Col unants things */
	application_clipboard_unant ();

	return trouble;
}

static gboolean
cmd_ins_del_row_col_redo (GnumericCommand *cmd, CommandContext *context)
{
	CmdInsDelRowCol *me = CMD_INS_DEL_ROW_COL(cmd);
	Range r;
	gboolean trouble;
	int index;

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->sizes == NULL, TRUE);
	g_return_val_if_fail (me->contents == NULL, TRUE);

	index = (me->is_insert)
	    ? (((me->is_cols) ? SHEET_MAX_COLS : SHEET_MAX_ROWS) - me->count)
	    : me->index;

	me->sizes = sheet_save_row_col_sizes (me->sheet, me->is_cols,
					      index, me->count);
	me->contents = clipboard_copy_range (me->sheet,
		(me->is_cols)
		? range_init (&r, index, 0, index + me->count - 1, SHEET_MAX_ROWS - 1)
		: range_init (&r, 0, index, SHEET_MAX_COLS-1,	index + me->count - 1));

	if (me->is_insert) {
		if (me->is_cols)
			trouble = sheet_insert_cols (context, me->sheet, me->index,
						     me->count, &me->reloc_storage);
		else
			trouble = sheet_insert_rows (context, me->sheet, me->index,
						     me->count, &me->reloc_storage);
	} else {
		if (me->is_cols)
			trouble = sheet_delete_cols (context, me->sheet, me->index,
						     me->count, &me->reloc_storage);
		else
			trouble =sheet_delete_rows (context, me->sheet, me->index,
						    me->count, &me->reloc_storage);
	}

	sheet_set_dirty (me->sheet, TRUE);
	workbook_recalc (me->sheet->workbook);
	sheet_update (me->sheet);

	/* Ins/Del Row/Col unants things */
	application_clipboard_unant ();

	return trouble;
}

static void
cmd_ins_del_row_col_destroy (GtkObject *cmd)
{
	CmdInsDelRowCol *me = CMD_INS_DEL_ROW_COL(cmd);

	if (me->sizes) {
		g_free (me->sizes);
		me->sizes = NULL;
	}
	if (me->contents) {
		clipboard_release (me->contents);
		me->contents = NULL;
	}
	if (me->reloc_storage) {
		workbook_expr_unrelocate_free (me->reloc_storage);
		me->reloc_storage = NULL;
	}
	gnumeric_command_destroy (cmd);
}

static gboolean
cmd_ins_del_row_col (CommandContext *context,
		     Sheet *sheet,
		     gboolean is_col, gboolean is_insert,
		     char const * descriptor, int index, int count)
{
	GtkObject *obj;
	CmdInsDelRowCol *me;

	g_return_val_if_fail (sheet != NULL, TRUE);

	obj = gtk_type_new (CMD_INS_DEL_ROW_COL_TYPE);
	me = CMD_INS_DEL_ROW_COL (obj);

	/* Store the specs for the object */
	me->sheet = sheet;
	me->is_cols = is_col;
	me->is_insert = is_insert;
	me->index = index;
	me->count = count;
	me->sizes = NULL;
	me->contents = NULL;

	me->parent.cmd_descriptor = descriptor;

	/* Register the command object */
	return command_push_undo (context, sheet->workbook, obj);
}

gboolean
cmd_insert_cols (CommandContext *context,
		 Sheet *sheet, int start_col, int count)
{
	char *mesg = g_strdup_printf ((count > 1)
				      ? _("Inserting %d columns before %s")
				      : _("Inserting %d column before %s"), count,
				      col_name(start_col));
	return cmd_ins_del_row_col (context, sheet, TRUE, TRUE, mesg,
				    start_col, count);
}

gboolean
cmd_insert_rows (CommandContext *context,
		 Sheet *sheet, int start_row, int count)
{
	char *mesg = g_strdup_printf ((count > 1)
				      ? _("Inserting %d rows before %d")
				      : _("Inserting %d row before %d"),
				      count, start_row+1);
	return cmd_ins_del_row_col (context, sheet, FALSE, TRUE, mesg,
				    start_row, count);
}

gboolean
cmd_delete_cols (CommandContext *context,
		 Sheet *sheet, int start_col, int count)
{
	char *mesg;
	if (count > 1) {
		/* col_name uses a static buffer */
		char *temp = g_strdup_printf (_("Deleting %d columns %s:"),
					      count, col_name(start_col));
		mesg = g_strconcat (temp, col_name(start_col+count-1), NULL);
		g_free (temp);
	} else
		mesg = g_strdup_printf (_("Deleting column %s"), col_name(start_col));

	return cmd_ins_del_row_col (context, sheet, TRUE, FALSE, mesg, start_col, count);
}

gboolean
cmd_delete_rows (CommandContext *context,
		 Sheet *sheet, int start_row, int count)
{
	char *mesg = (count > 1)
	    ? g_strdup_printf (_("Deleting %d rows %d:%d"), count, start_row,
			       start_row+count-1)
	    : g_strdup_printf (_("Deleting row %d"), start_row);

	return cmd_ins_del_row_col (context, sheet, FALSE, FALSE, mesg, start_row, count);
}

/******************************************************************/

#define CMD_CLEAR_TYPE        (cmd_clear_get_type ())
#define CMD_CLEAR(o)          (GTK_CHECK_CAST ((o), CMD_CLEAR_TYPE, CmdClear))

typedef struct
{
	GnumericCommand parent;

	int	 clear_flags;
	int	 paste_flags;
	Sheet	*sheet;
	GSList 	*old_content;
	GSList	*selection;
} CmdClear;

GNUMERIC_MAKE_COMMAND (CmdClear, cmd_clear);

static gboolean
cmd_clear_undo (GnumericCommand *cmd, CommandContext *context)
{
	CmdClear *me = CMD_CLEAR(cmd);
	GSList *ranges;

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->selection != NULL, TRUE);
	g_return_val_if_fail (me->old_content != NULL, TRUE);

	for (ranges = me->selection; ranges != NULL ; ranges = ranges->next) {
		Range const * const r = ranges->data;
		PasteTarget pt;
		CellRegion * c;

		g_return_val_if_fail (me->old_content != NULL, TRUE);

		c = me->old_content->data;
		clipboard_paste_region (context,
					paste_target_init (&pt, me->sheet, r, me->paste_flags),
					c);
		clipboard_release (c);
		me->old_content = g_slist_remove (me->old_content, c);
	}
	g_return_val_if_fail (me->old_content == NULL, TRUE);

	sheet_set_dirty (me->sheet, TRUE);
	workbook_recalc (me->sheet->workbook);
	sheet_update (me->sheet);

	return FALSE;
}

static gboolean
cmd_clear_redo (GnumericCommand *cmd, CommandContext *context)
{
	CmdClear *me = CMD_CLEAR(cmd);
	GSList *l;

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->selection != NULL, TRUE);
	g_return_val_if_fail (me->old_content == NULL, TRUE);

	/* Check for array subdivision */
	if (selection_check_for_array (me->sheet, me->selection)) {
		gnumeric_error_splits_array (context, _("Undo Clear"));
		return TRUE;
	}

	for (l = me->selection ; l != NULL ; l = l->next) {
		Range const * const r = l->data;
		me->old_content =
			g_slist_prepend (me->old_content,
				clipboard_copy_range (me->sheet, r));

		/* We have already checked the arrays */
		sheet_clear_region (context, me->sheet,
				    r->start.col, r->start.row,
				    r->end.col, r->end.row,
				    me->clear_flags|CLEAR_NOCHECKARRAY);
	}

	sheet_set_dirty (me->sheet, TRUE);
	workbook_recalc (me->sheet->workbook);
	sheet_update (me->sheet);

	return FALSE;
}

static void
cmd_clear_destroy (GtkObject *cmd)
{
	CmdClear *me = CMD_CLEAR(cmd);

	if (me->old_content != NULL) {
		GSList *l;
		for (l = me->old_content ; l != NULL ; l = g_slist_remove (l, l->data))
			clipboard_release (l->data);
		me->old_content = NULL;
	}
	if (me->selection != NULL) {
		GSList *l;
		for (l = me->selection ; l != NULL ; l = g_slist_remove (l, l->data))
			g_free (l->data);
		me->selection = NULL;
	}

	gnumeric_command_destroy (cmd);
}

gboolean
cmd_clear_selection (CommandContext *context, Sheet *sheet, int clear_flags)
{
	GtkObject *obj;
	CmdClear *me;

	g_return_val_if_fail (sheet != NULL, TRUE);

	obj = gtk_type_new (CMD_CLEAR_TYPE);
	me = CMD_CLEAR (obj);

	/* Store the specs for the object */
	me->sheet = sheet;
	me->clear_flags = clear_flags;
	me->old_content = NULL;
	me->selection = selection_get_ranges (sheet, FALSE /* No intersection */);

	me->paste_flags = 0;
	if (clear_flags & CLEAR_VALUES)
		me->paste_flags |= PASTE_FORMULAS;
	if (clear_flags & CLEAR_FORMATS)
		me->paste_flags |= PASTE_FORMATS;
	if (clear_flags & CLEAR_COMMENTS)
		g_warning ("Deleted comments can not be restored yet");

	/* TODO : Something more descriptive ? maybe the range name */
	me->parent.cmd_descriptor = g_strdup (_("Clear"));

	/* Register the command object */
	return command_push_undo (context, sheet->workbook, obj);
}

/******************************************************************/

#define CMD_FORMAT_TYPE        (cmd_format_get_type ())
#define CMD_FORMAT(o)          (GTK_CHECK_CAST ((o), CMD_FORMAT_TYPE, CmdFormat))

typedef struct {
	CellPos pos;
	GList  *styles;
} CmdFormatOldStyle;

typedef struct {
	GnumericCommand parent;

	Sheet         *sheet;
	GSList        *selection;

	GSList        *old_styles;

	MStyle        *new_style;
	MStyleBorder **borders;
} CmdFormat;

GNUMERIC_MAKE_COMMAND (CmdFormat, cmd_format);

static gboolean
cmd_format_undo (GnumericCommand *cmd, CommandContext *context)
{
	CmdFormat *me = CMD_FORMAT (cmd);

	g_return_val_if_fail (me != NULL, TRUE);

	if (me->old_styles) {
		GSList *l1 = me->old_styles;
		GSList *l2 = me->selection;

		for (; l1; l1 = l1->next, l2 = l2->next) {
			Range *r;
			CmdFormatOldStyle *os = l1->data;
			SpanCalcFlags flags = 
				sheet_style_attach_list (me->sheet, os->styles,
							 &os->pos, FALSE);

			g_return_val_if_fail (l2 && l2->data, TRUE);

			r = l2->data;
			sheet_range_calc_spans (me->sheet, *r, flags);
			if (flags != SPANCALC_SIMPLE)
				rows_height_update (me->sheet, r);
		}
	}
	
	sheet_set_dirty (me->sheet, TRUE);
	workbook_recalc (me->sheet->workbook);
	sheet_update (me->sheet);

	return FALSE;
}

static gboolean
cmd_format_redo (GnumericCommand *cmd, CommandContext *context)
{
	CmdFormat *me = CMD_FORMAT (cmd);
	GSList    *l;

	g_return_val_if_fail (me != NULL, TRUE);

	for (l = me->selection; l; l = l->next) {
		if (me->borders)
			sheet_range_set_border (me->sheet, l->data,
						me->borders);
		if (me->new_style) {
			mstyle_ref (me->new_style);
			sheet_range_apply_style (me->sheet, l->data,
						 me->new_style);
		}
	}

	sheet_set_dirty (me->sheet, TRUE);
	workbook_recalc (me->sheet->workbook);
	sheet_update (me->sheet);

	return FALSE;
}

static void
cmd_format_destroy (GtkObject *cmd)
{
	CmdFormat *me = CMD_FORMAT (cmd);
	int        i;

	if (me->new_style)
		mstyle_unref (me->new_style);
	me->new_style = NULL;

	if (me->borders) {
		for (i = STYLE_BORDER_TOP; i < STYLE_BORDER_EDGE_MAX; i++)
			style_border_unref (me->borders [i]);
		g_free (me->borders);
		me->borders = NULL;
	}

	if (me->old_styles != NULL) {
		GSList *l;

		for (l = me->old_styles ; l != NULL ; l = g_slist_remove (l, l->data)) {
			CmdFormatOldStyle *os = l->data;

			if (os->styles)
				sheet_style_list_destroy (os->styles);

			g_free (os);
		}
		me->old_styles = NULL;
	}

	if (me->selection != NULL) {
		GSList *l;
		for (l = me->selection ; l != NULL ; l = g_slist_remove (l, l->data))
			g_free (l->data);
		me->selection = NULL;
	}

	gnumeric_command_destroy (cmd);
}

/**
 * cmd_format:
 * @context: the context.
 * @sheet: the sheet
 * @style: style to apply to the selection
 * @borders: borders to apply to the selection
 * 
 *  If borders is non NULL, then the MStyleBorder references are passed,
 * the MStyle reference is also passed.
 * 
 * Return value: TRUE if there was a problem
 **/
gboolean
cmd_format (CommandContext *context, Sheet *sheet,
	    MStyle *style, MStyleBorder **borders)
{
	GtkObject *obj;
	CmdFormat *me;
	GSList    *l;

	g_return_val_if_fail (sheet != NULL, TRUE);

	obj = gtk_type_new (CMD_FORMAT_TYPE);
	me = CMD_FORMAT (obj);

	me->sheet      = sheet;
	me->selection  = selection_get_ranges (sheet, FALSE); /* TRUE ? */
	me->new_style  = style;

	me->old_styles = NULL;
	for (l = me->selection; l; l = l->next) {
		CmdFormatOldStyle *os;
		Range range = *((Range const *)l->data);

		/* Store the containing range to handle borders */
		if (range.start.col > 0) range.start.col--;
		if (range.start.row > 0) range.start.row--;
		if (range.end.col < SHEET_MAX_COLS-1) range.end.col++;
		if (range.end.row < SHEET_MAX_ROWS-1) range.end.row++;

		os = g_new (CmdFormatOldStyle, 1);

		os->styles = sheet_get_styles_in_range (sheet, &range);
		os->pos = range.start;

		me->old_styles = g_slist_append (me->old_styles, os);
	}

	if (borders) {
		int i;

		me->borders = g_new (MStyleBorder *, STYLE_BORDER_EDGE_MAX);
		for (i = STYLE_BORDER_TOP; i < STYLE_BORDER_EDGE_MAX; i++)
			me->borders [i] = borders [i];
	} else
		me->borders = NULL;

	me->parent.cmd_descriptor = g_strdup (_("Format cells"));

	/* Register the command object */
	return command_push_undo (context, sheet->workbook, obj);
}

/******************************************************************/

#define CMD_RENAME_SHEET_TYPE        (cmd_rename_sheet_get_type ())
#define CMD_RENAME_SHEET(o)          (GTK_CHECK_CAST ((o), CMD_RENAME_SHEET_TYPE, CmdRenameSheet))

typedef struct
{
	GnumericCommand parent;

	Workbook *wb;
	char *old_name, *new_name;
} CmdRenameSheet;

GNUMERIC_MAKE_COMMAND (CmdRenameSheet, cmd_rename_sheet);

static gboolean
cmd_rename_sheet_undo (GnumericCommand *cmd, CommandContext *context)
{
	CmdRenameSheet *me = CMD_RENAME_SHEET (cmd);

	g_return_val_if_fail (me != NULL, TRUE);

	return workbook_rename_sheet (context, me->wb,
				      me->new_name, me->old_name);
}

static gboolean
cmd_rename_sheet_redo (GnumericCommand *cmd, CommandContext *context)
{
	CmdRenameSheet *me = CMD_RENAME_SHEET (cmd);

	g_return_val_if_fail (me != NULL, TRUE);

	return workbook_rename_sheet (context, me->wb,
				      me->old_name, me->new_name);
}
static void
cmd_rename_sheet_destroy (GtkObject *cmd)
{
	CmdRenameSheet *me = CMD_RENAME_SHEET (cmd);

	me->wb = NULL;
	g_free (me->old_name);
	g_free (me->new_name);
	gnumeric_command_destroy (cmd);
}

gboolean
cmd_rename_sheet (CommandContext *context,
		  Workbook *wb, const char *old_name, const char *new_name)
{
	GtkObject *obj;
	CmdRenameSheet *me;

	g_return_val_if_fail (wb != NULL, TRUE);

	obj = gtk_type_new (CMD_RENAME_SHEET_TYPE);
	me = CMD_RENAME_SHEET (obj);

	/* Store the specs for the object */
	me->wb = wb;
	me->old_name = g_strdup (old_name);
	me->new_name = g_strdup (new_name);

	me->parent.cmd_descriptor = 
	    g_strdup_printf (_("Rename sheet '%s' '%s'"), old_name, new_name);

	/* Register the command object */
	return command_push_undo (context, wb, obj);
}

/******************************************************************/

#define CMD_SET_DATE_TIME_TYPE        (cmd_set_date_time_get_type ())
#define CMD_SET_DATE_TIME(o)          (GTK_CHECK_CAST ((o), CMD_SET_DATE_TIME_TYPE, CmdSetDateTime))

typedef struct
{
	GnumericCommand parent;

	gboolean	 is_date;
	EvalPos	 pos;
	gchar		*contents;
} CmdSetDateTime;

GNUMERIC_MAKE_COMMAND (CmdSetDateTime, cmd_set_date_time);

static gboolean
cmd_set_date_time_undo (GnumericCommand *cmd, CommandContext *context)
{
	CmdSetDateTime *me = CMD_SET_DATE_TIME (cmd);
	Cell  *cell;
	Sheet *sheet;

	g_return_val_if_fail (me != NULL, TRUE);

	sheet = me->pos.sheet;

	/* Get the cell */
	cell = sheet_cell_get (sheet, me->pos.eval.col, me->pos.eval.row);

	/* The cell MUST exist or something is very confused */
	g_return_val_if_fail (cell != NULL, TRUE);

	/* Restore the old value (possibly empty) */
	if (me->contents != NULL) {
		sheet_cell_set_text (cell, me->contents);
		g_free (me->contents);
		me->contents = NULL;
	} else
		sheet_cell_remove (sheet, cell, TRUE);

	/* see if we need to update status */
	sheet_flag_status_update_cell (me->pos.sheet,
				       me->pos.eval.col, me->pos.eval.row);

	sheet_set_dirty (sheet, TRUE);
	workbook_recalc (sheet->workbook);
	sheet_update (sheet);

	return FALSE;
}

static gboolean
cmd_set_date_time_redo (GnumericCommand *cmd, CommandContext *context)
{
	CmdSetDateTime *me = CMD_SET_DATE_TIME (cmd);
	Value *v;
	Cell *cell;
	char const * prefered_format;

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->contents == NULL, TRUE);

	if (me->is_date) {
		v = value_new_int (datetime_timet_to_serial (time (NULL)));

		/* FIXME : the '>' prefix is intended to give the translators
		 * a chance to provide a locale specific date format.
		 * This is ugly because the format may not show up in the
		 * list of date formats, and will be marked custom.  In addition
		 * translators should be aware that the leading character of the
		 * result will be ignored.
		 */
		prefered_format = _(">mm/dd/yyyy");
	} else {
		v = value_new_float (datetime_timet_to_seconds (time (NULL)) / (24.0 * 60 * 60));

		/* FIXME : See comment above */
		prefered_format = _(">hh:mm");
	}

	/* Get the cell (creating it if needed) */
	cell = sheet_cell_fetch (me->pos.sheet, me->pos.eval.col, me->pos.eval.row);

	/* Save contents */
	me->contents = (cell->value) ? cell_get_entered_text (cell) : NULL;

	sheet_cell_set_value (cell, v, prefered_format+1);

	/* see if we need to update status */
	sheet_flag_status_update_cell (me->pos.sheet,
				       me->pos.eval.col, me->pos.eval.row);

	sheet_set_dirty (me->pos.sheet, TRUE);
	workbook_recalc (me->pos.sheet->workbook);
	sheet_update (me->pos.sheet);

	return FALSE;
}
static void
cmd_set_date_time_destroy (GtkObject *cmd)
{
	CmdSetDateTime *me = CMD_SET_DATE_TIME (cmd);

	if (me->contents) {
		g_free (me->contents);
		me->contents = NULL;
	}
	gnumeric_command_destroy (cmd);
}

gboolean
cmd_set_date_time (CommandContext *context,
		   Sheet *sheet, CellPos const *pos, gboolean is_date)
{
	GtkObject *obj;
	CmdSetDateTime *me;
	Cell const *cell;

	g_return_val_if_fail (sheet != NULL, TRUE);

	/* Ensure that we are not splitting up an array */
	cell = sheet_cell_get (sheet, pos->col, pos->row);
	if (cell_is_partial_array (cell)) {
		gnumeric_error_splits_array (context, _("Set Date/Time"));
		return TRUE;
	}

	obj = gtk_type_new (CMD_SET_DATE_TIME_TYPE);
	me = CMD_SET_DATE_TIME (obj);

	/* Store the specs for the object */
	me->pos.sheet = sheet;
	me->pos.eval = *pos;
	me->is_date = is_date;
	me->contents = NULL;

	me->parent.cmd_descriptor =
	    g_strdup_printf (is_date
			     ? _("Setting current date in %s")
			     : _("Setting current time in %s"),
			     cell_coord_name(pos->col, pos->row));

	/* Register the command object */
	return command_push_undo (context, sheet->workbook, obj);
}

/******************************************************************/

#define CMD_RESIZE_ROW_COL_TYPE        (cmd_resize_row_col_get_type ())
#define CMD_RESIZE_ROW_COL(o)          (GTK_CHECK_CAST ((o), CMD_RESIZE_ROW_COL_TYPE, CmdResizeRowCol))

typedef struct
{
	GnumericCommand parent;

	Sheet		*sheet;
	gboolean	 is_col;
	int		 index;
	double		*sizes;
} CmdResizeRowCol;

GNUMERIC_MAKE_COMMAND (CmdResizeRowCol, cmd_resize_row_col);

static gboolean
cmd_resize_row_col_undo (GnumericCommand *cmd, CommandContext *context)
{
	CmdResizeRowCol *me = CMD_RESIZE_ROW_COL (cmd);

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->sizes != NULL, TRUE);

	/* restore row/col sizes */
	sheet_restore_row_col_sizes (me->sheet, me->is_col, me->index, 1,
				     me->sizes);
	me->sizes = NULL;

	return FALSE;
}

static gboolean
cmd_resize_row_col_redo (GnumericCommand *cmd, CommandContext *context)
{
	CmdResizeRowCol *me = CMD_RESIZE_ROW_COL (cmd);

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->sizes == NULL, TRUE);

	me->sizes = sheet_save_row_col_sizes (me->sheet, me->is_col,
					      me->index, 1);
	return FALSE;
}
static void
cmd_resize_row_col_destroy (GtkObject *cmd)
{
	CmdResizeRowCol *me = CMD_RESIZE_ROW_COL (cmd);

	if (me->sizes) {
		g_free (me->sizes);
		me->sizes = NULL;
	}
	gnumeric_command_destroy (cmd);
}

gboolean
cmd_resize_row_col (CommandContext *context,
		    Sheet *sheet, int index, gboolean is_col)
{
	GtkObject *obj;
	CmdResizeRowCol *me;

	g_return_val_if_fail (sheet != NULL, TRUE);

	obj = gtk_type_new (CMD_RESIZE_ROW_COL_TYPE);
	me = CMD_RESIZE_ROW_COL (obj);

	/* Store the specs for the object */
	me->sheet = sheet;
	me->is_col = is_col;
	me->index = index;
	me->sizes = NULL;

	me->parent.cmd_descriptor = is_col
	    ? g_strdup_printf (_("Setting width of column %s"), col_name(index))
	    : g_strdup_printf (_("Setting height of row %d"), index+1);

	/* TODO :
	 * - Patch into manual and auto resizing 
	 * - store the selected sized,
	 */

	/* Register the command object */
	return command_push_undo (context, sheet->workbook, obj);
}

/******************************************************************/

#define CMD_SORT_TYPE        (cmd_sort_get_type ())
#define CMD_SORT(o)          (GTK_CHECK_CAST ((o), CMD_SORT_TYPE, CmdSort))

typedef struct
{
	GnumericCommand parent;

	SortData   *data;
	int        *perm;
	int        *inv;
} CmdSort;

GNUMERIC_MAKE_COMMAND (CmdSort, cmd_sort);

static void
cmd_sort_destroy (GtkObject *cmd)
{
	CmdSort *me = CMD_SORT (cmd);

	sort_data_destroy (me->data);
		
	gnumeric_command_destroy (cmd);
}

static gboolean
cmd_sort_undo (GnumericCommand *cmd, CommandContext *context)
{
	CmdSort *me = CMD_SORT (cmd);
	int length, i;
	int *inv;
	
	g_return_val_if_fail (me != NULL, TRUE);

	if (!me->inv) {
		if (me->data->top) {
			length = me->data->range->end.row - 
				me->data->range->start.row + 1;
		} else {
			length = me->data->range->end.col - 
				me->data->range->start.col + 1;
		}

		me->inv = g_new (int, length);
		for (i=0; i <length; i++) {
			me->inv[me->perm[i]] = i;
		}
	}
	
	sort_position (context, me->data, me->inv);

	sheet_set_dirty (me->data->sheet, TRUE);
	workbook_recalc (me->data->sheet->workbook);
	sheet_update (me->data->sheet);

	return FALSE;
}

static gboolean
cmd_sort_redo (GnumericCommand *cmd, CommandContext *context)
{
	CmdSort *me = CMD_SORT (cmd);

	g_return_val_if_fail (me != NULL, TRUE);

	if (!me->perm) {
		me->perm = sort_contents (context, me->data);
	} else {
		sort_position (context, me->data, me->perm);
	}

	sheet_set_dirty (me->data->sheet, TRUE);
	workbook_recalc (me->data->sheet->workbook);
	sheet_update (me->data->sheet);

	return FALSE;
}

gboolean
cmd_sort (CommandContext *context, SortData *data)
{
	GtkObject *obj;
	CmdSort *me;
	
	g_return_val_if_fail (data != NULL, TRUE);

	obj = gtk_type_new (CMD_SORT_TYPE);
	me = CMD_SORT (obj);

	me->data = data;
	me->perm = NULL;
	me->inv = NULL;

	me->parent.cmd_descriptor =
		g_strdup_printf (_("Sorting %s"), range_name(me->data->range));

	/* Register the command object */
	return command_push_undo (context, me->data->sheet->workbook, obj);
}

/******************************************************************/

#define CMD_HIDE_ROW_COL_TYPE        (cmd_hide_row_col_get_type ())
#define CMD_HIDE_ROW_COL(o)          (GTK_CHECK_CAST ((o), CMD_HIDE_ROW_COL_TYPE, CmdHideRowCol))

typedef struct
{
	GnumericCommand parent;

	Sheet         *sheet;
	gboolean       is_cols;
	gboolean       visible;
	ColRowVisList  elements;
} CmdHideRowCol;

GNUMERIC_MAKE_COMMAND (CmdHideRowCol, cmd_hide_row_col);

static gboolean
cmd_hide_row_col_undo (GnumericCommand *cmd, CommandContext *context)
{
	CmdHideRowCol *me = CMD_HIDE_ROW_COL (cmd);

	g_return_val_if_fail (me != NULL, TRUE);

	col_row_set_visiblity (me->sheet, me->is_cols,
			       !me->visible, me->elements);

	sheet_set_dirty (me->sheet, TRUE);
	sheet_update (me->sheet);

	return FALSE;
}

static gboolean
cmd_hide_row_col_redo (GnumericCommand *cmd, CommandContext *context)
{
	CmdHideRowCol *me = CMD_HIDE_ROW_COL (cmd);

	g_return_val_if_fail (me != NULL, TRUE);

	col_row_set_visiblity (me->sheet, me->is_cols,
			       me->visible, me->elements);

	sheet_set_dirty (me->sheet, TRUE);
	sheet_update (me->sheet);

	return FALSE;
}

static void
cmd_hide_row_col_destroy (GtkObject *cmd)
{
	CmdHideRowCol *me = CMD_HIDE_ROW_COL (cmd);
	me->elements = col_row_vis_list_destroy (me->elements);
	gnumeric_command_destroy (cmd);
}

gboolean
cmd_hide_selection_rows_cols (CommandContext *context, Sheet *sheet,
			      gboolean is_cols, gboolean visible)
{
	GtkObject *obj;
	CmdHideRowCol *me;
	
	g_return_val_if_fail (sheet != NULL, TRUE);

	obj = gtk_type_new (CMD_HIDE_ROW_COL_TYPE);
	me = CMD_HIDE_ROW_COL (obj);
	
	me->sheet = sheet;
	me->is_cols = is_cols;
	me->visible = visible;
	me->elements = col_row_get_visiblity_toggle (sheet, is_cols, visible);
	
	me->parent.cmd_descriptor = g_strdup (is_cols
		? (visible ? _("Unhide columns") : _("Hide columns"))
		: (visible ? _("Unhide rows") : _("Hide rows")));

	/* Register the command object */
	return command_push_undo (context, sheet->workbook, obj);
}

/******************************************************************/

#define CMD_PASTE_CUT_TYPE        (cmd_paste_cut_get_type ())
#define CMD_PASTE_CUT(o)          (GTK_CHECK_CAST ((o), CMD_PASTE_CUT_TYPE, CmdPasteCut))

typedef struct
{
	GnumericCommand parent;

	ExprRelocateInfo info;
	GSList		*paste_content;
	GSList		*reloc_storage;
} CmdPasteCut;

GNUMERIC_MAKE_COMMAND (CmdPasteCut, cmd_paste_cut);

typedef struct
{
	PasteTarget pt;
	CellRegion *contents;
} PasteContent;

static gboolean
cmd_paste_cut_undo (GnumericCommand *cmd, CommandContext *context)
{
	CmdPasteCut *me = CMD_PASTE_CUT(cmd);
	ExprRelocateInfo reverse;
	GSList *tmp = NULL;

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->paste_content != NULL, TRUE);

	reverse.target_sheet = me->info.origin_sheet;
	reverse.origin_sheet = me->info.target_sheet;
	reverse.origin = me->info.origin;
	range_translate (&reverse.origin,
			 me->info.col_offset,
			 me->info.row_offset);
	reverse.col_offset = -me->info.col_offset;
	reverse.row_offset = -me->info.row_offset;

	/* Move things back, and throw away the undo info */
	sheet_move_range (context, &reverse, &tmp);
	workbook_expr_unrelocate_free (tmp);

	while (me->paste_content) {
		PasteContent *pc = me->paste_content->data;
		me->paste_content = g_slist_remove (me->paste_content, pc);

		clipboard_paste_region (context, &pc->pt, pc->contents);
		clipboard_release (pc->contents);
		g_free (pc);
	}

	/* Restore the changed expressions */
	workbook_expr_unrelocate (me->info.target_sheet->workbook,
				  me->reloc_storage);
	me->reloc_storage = NULL;

	/* Force update of the status area */
	sheet_flag_status_update_range (me->info.target_sheet, NULL /* force update */);

	/* Select the original region */
	sheet_selection_set (me->info.origin_sheet,
			     me->info.origin.start.col,
			     me->info.origin.start.row,
			     me->info.origin.start.col,
			     me->info.origin.start.row,
			     me->info.origin.end.col,
			     me->info.origin.end.row);

	sheet_set_dirty (me->info.target_sheet, TRUE);
	workbook_recalc (me->info.target_sheet->workbook);
	sheet_update (me->info.target_sheet);

	return FALSE;
}

static gboolean
cmd_paste_cut_redo (GnumericCommand *cmd, CommandContext *context)
{
	CmdPasteCut *me = CMD_PASTE_CUT(cmd);
	Range  tmp, valid_range;
	GList *frag;

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->paste_content == NULL, TRUE);
	g_return_val_if_fail (me->reloc_storage == NULL, TRUE);

	tmp = me->info.origin;
	range_normalize (&tmp);
	tmp.start.col += me->info.col_offset;
	tmp.end.col   += me->info.col_offset;
	tmp.start.row += me->info.row_offset;
	tmp.end.row   += me->info.row_offset;
	(void) range_init (&valid_range, 0, 0, SHEET_MAX_COLS-1, SHEET_MAX_ROWS-1);

	/*
	 * need to store any portions of src content that are moving off the
	 * sheet.
	 */
	frag = range_split_ranges (&valid_range, &tmp, NULL);
	while (frag) {
		PasteContent *pc = g_new (PasteContent, 1);
		Range *r = frag->data;
		frag = g_list_remove (frag, r);

		if (!range_overlap (&valid_range, r))
			(void) range_translate (r, -me->info.col_offset,
						-me->info.row_offset);

		/* Store the original contents */
		paste_target_init (&pc->pt, me->info.target_sheet, r, PASTE_ALL_TYPES);
		pc->contents = clipboard_copy_range (me->info.target_sheet,  r);
		me->paste_content = g_slist_prepend (me->paste_content, pc);
	}

	if (tmp.start.col < 0)
		tmp.start.col = 0;
	if (tmp.start.row < 0)
		tmp.start.row = 0;
	if (tmp.end.col >= SHEET_MAX_COLS)
		tmp.end.col = SHEET_MAX_COLS-1;
	if (tmp.end.row >= SHEET_MAX_ROWS)
		tmp.end.row = SHEET_MAX_ROWS-1;

	/* Make sure the destination is selected */
	sheet_selection_set (me->info.target_sheet,
			     tmp.start.col, tmp.start.row,
			     tmp.start.col, tmp.start.row,
			     tmp.end.col, tmp.end.row);

	sheet_move_range (context, &me->info, &me->reloc_storage);

	sheet_set_dirty (me->info.target_sheet, TRUE);
	workbook_recalc (me->info.target_sheet->workbook);
	sheet_update (me->info.target_sheet);

	return FALSE;
}
static void
cmd_paste_cut_destroy (GtkObject *cmd)
{
	CmdPasteCut *me = CMD_PASTE_CUT(cmd);

	while (me->paste_content) {
		PasteContent *pc = me->paste_content->data;
		me->paste_content = g_slist_remove (me->paste_content, pc);
		clipboard_release (pc->contents);
		g_free (pc);
	}
	if (me->reloc_storage) {
		workbook_expr_unrelocate_free (me->reloc_storage);
		me->reloc_storage = NULL;
	}
	gnumeric_command_destroy (cmd);
}

gboolean
cmd_paste_cut (CommandContext *context, ExprRelocateInfo const *info)
{
	GtkObject *obj;
	CmdPasteCut *me;

	/* FIXME : improve on this */
	char *descriptor = g_strdup_printf (_("Moving cells") );

	g_return_val_if_fail (info != NULL, TRUE);

	obj = gtk_type_new (CMD_PASTE_CUT_TYPE);
	me = CMD_PASTE_CUT (obj);

	/* Store the specs for the object */
	me->info = *info;
	me->paste_content = NULL;
	me->reloc_storage = NULL;

	me->parent.cmd_descriptor = descriptor;

	/* NOTE : if the destination workbook is different from the source workbook
	 * should we have undo elements in both menus ??  It seems poor form to
	 * hit undo in 1 window and effect another ...
	 *
	 * Maybe queue it as 2 different commands, as a clear in one book and
	 * a paste  in the other.    This is not symetric though.  What happens to the
	 * cells in the original sheet that now reference the cells in the other.
	 * When do they reset to the original ?
	 *
	 * Probably when the clear in the original is undone.
	 */

	/* Register the command object */
	return command_push_undo (context, info->target_sheet->workbook, obj);
}

/******************************************************************/

#define CMD_PASTE_COPY_TYPE        (cmd_paste_copy_get_type ())
#define CMD_PASTE_COPY(o)          (GTK_CHECK_CAST ((o), CMD_PASTE_COPY_TYPE, CmdPasteCopy))

typedef struct
{
	GnumericCommand parent;

	CellRegion *content;
	PasteTarget dst;
	gboolean    has_been_through_cycle;
} CmdPasteCopy;

GNUMERIC_MAKE_COMMAND (CmdPasteCopy, cmd_paste_copy);

static gboolean
cmd_paste_copy_undo (GnumericCommand *cmd, CommandContext *context)
{
	CmdPasteCopy *me = CMD_PASTE_COPY(cmd);
	CellRegion *content;

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->content != NULL, TRUE);

	content = clipboard_copy_range (me->dst.sheet, &me->dst.range);
	if (clipboard_paste_region (context, &me->dst, me->content)) {
		/* There was a problem, avoid leaking */
		clipboard_release (content);
		return TRUE;
	}

	if (me->has_been_through_cycle)
		clipboard_release (me->content);
	else
		me->dst.paste_flags &= ~PASTE_TRANSPOSE;

	me->content = content;
	me->has_been_through_cycle = TRUE;

	/* Make the newly pasted content the selection (this queues a redraw) */
	sheet_selection_reset_only (me->dst.sheet);
	sheet_selection_add_range (me->dst.sheet,
				   me->dst.range.start.col, me->dst.range.start.row,
				   me->dst.range.start.col, me->dst.range.start.row,
				   me->dst.range.end.col, me->dst.range.end.row);

	sheet_set_dirty (me->dst.sheet, TRUE);
	workbook_recalc (me->dst.sheet->workbook);
	sheet_update (me->dst.sheet);

	return FALSE;
}

static gboolean
cmd_paste_copy_redo (GnumericCommand *cmd, CommandContext *context)
{
	return cmd_paste_copy_undo (cmd, context);
}
static void
cmd_paste_copy_destroy (GtkObject *cmd)
{
	CmdPasteCopy *me = CMD_PASTE_COPY(cmd);

	if (me->content) {
		if (me->has_been_through_cycle)
			clipboard_release (me->content);
		me->content = NULL;
	}
	gnumeric_command_destroy (cmd);
}

gboolean
cmd_paste_copy (CommandContext *context,
		PasteTarget const *pt, CellRegion *content)
{
	GtkObject *obj;
	CmdPasteCopy *me;

	g_return_val_if_fail (pt != NULL, TRUE);
	g_return_val_if_fail (pt->sheet != NULL, TRUE);

	obj = gtk_type_new (CMD_PASTE_COPY_TYPE);
	me = CMD_PASTE_COPY (obj);

	/* Store the specs for the object */
	me->dst = *pt;
	me->content = content;
	me->has_been_through_cycle = FALSE;

	/* If the destination is a singleton paste the entire content */
	if (range_is_singleton (&me->dst.range)) {
		if (pt->paste_flags & PASTE_TRANSPOSE) {
			me->dst.range.end.col = me->dst.range.start.col + content->rows -1;
			me->dst.range.end.row = me->dst.range.start.row + content->cols -1;
		} else {
			me->dst.range.end.col = me->dst.range.start.col + content->cols -1;
			me->dst.range.end.row = me->dst.range.start.row + content->rows -1;
		}
	} else if (pt->paste_flags & PASTE_TRANSPOSE) {
		/* when transposed single rows or cols get replicated as needed */
		if (content->cols == 1 && me->dst.range.start.col == me->dst.range.end.col) {
			me->dst.range.end.col = me->dst.range.start.col + content->rows -1;
		} else if (content->rows == 1 && me->dst.range.start.row == me->dst.range.end.row) {
			me->dst.range.end.row = me->dst.range.start.row + content->cols -1;
		}
	}

	me->parent.cmd_descriptor = g_strdup_printf (_("Pasting into %s"), range_name(&pt->range));

	/* Register the command object */
	return command_push_undo (context, pt->sheet->workbook, obj);
}

/******************************************************************/

#define CMD_AUTOFILL_TYPE        (cmd_autofill_get_type ())
#define CMD_AUTOFILL(o)          (GTK_CHECK_CAST ((o), CMD_AUTOFILL_TYPE, CmdAutofill))

typedef struct
{
	GnumericCommand parent;

	CellRegion *content;
	PasteTarget dst;
	int base_col, base_row, w, h, end_col, end_row;
} CmdAutofill;

GNUMERIC_MAKE_COMMAND (CmdAutofill, cmd_autofill);

static gboolean
cmd_autofill_undo (GnumericCommand *cmd, CommandContext *context)
{
	CmdAutofill *me = CMD_AUTOFILL(cmd);
	gboolean res;

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->content != NULL, TRUE);

	res = clipboard_paste_region (context, &me->dst, me->content);
	clipboard_release (me->content);
	me->content = NULL;

	if (res)
		return TRUE;

	/* Make the newly pasted content the selection (this queues a redraw) */
	sheet_selection_reset_only (me->dst.sheet);
	sheet_selection_add_range (me->dst.sheet,
				   me->base_col, me->base_row,
				   me->base_col, me->base_row,
				   me->base_col + me->w-1,
				   me->base_row + me->h-1);

	sheet_set_dirty (me->dst.sheet, TRUE);
	workbook_recalc (me->dst.sheet->workbook);
	sheet_update (me->dst.sheet);

	return FALSE;
}

static gboolean
cmd_autofill_redo (GnumericCommand *cmd, CommandContext *context)
{
	CmdAutofill *me = CMD_AUTOFILL(cmd);

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->content == NULL, TRUE);

	me->content = clipboard_copy_range (me->dst.sheet, &me->dst.range);
	sheet_autofill (me->dst.sheet,
			me->base_col, me->base_row, me->w, me->h,
			me->end_col, me->end_row);

	/* Make the newly filled content the selection (this queues a redraw) */
	sheet_selection_reset_only (me->dst.sheet);
	sheet_selection_add_range (me->dst.sheet,
				   me->base_col, me->base_row,
				   me->base_col, me->base_row,
				   me->end_col, me->end_row);

	sheet_set_dirty (me->dst.sheet, TRUE);
	workbook_recalc (me->dst.sheet->workbook);
	sheet_update (me->dst.sheet);

	return FALSE;
}

static void
cmd_autofill_destroy (GtkObject *cmd)
{
	CmdAutofill *me = CMD_AUTOFILL(cmd);

	if (me->content) {
		clipboard_release (me->content);
		me->content = NULL;
	}
	gnumeric_command_destroy (cmd);
}

gboolean
cmd_autofill (CommandContext *context, Sheet *sheet,
	      int base_col, int base_row,
	      int w, int h, int end_col, int end_row)
{
	GtkObject *obj;
	CmdAutofill *me;

	g_return_val_if_fail (sheet != NULL, TRUE);

	obj = gtk_type_new (CMD_AUTOFILL_TYPE);
	me = CMD_AUTOFILL (obj);

	/* Store the specs for the object */
	me->content = NULL;
	me->dst.sheet = sheet;
	me->dst.paste_flags = PASTE_FORMULAS | PASTE_FORMATS;

	/* FIXME : We can copy less than this */
	range_init (&me->dst.range,  base_col, base_row, end_col, end_row);
	me->base_col = base_col;
	me->base_row = base_row,
	me->w = w;
	me->h = h;
	me->end_col = end_col;
	me->end_row = end_row;

	me->parent.cmd_descriptor = g_strdup (_("Autofill"));

	/* Register the command object */
	return command_push_undo (context, sheet->workbook, obj);
}

/******************************************************************/

/*
 * - Complete colrow resize
 *
 * TODO : Make a list of commands that should have undo support
 *        that do not even have stubs
 *
 * - SheetObject creation & manipulation.
 */

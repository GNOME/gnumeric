/* vim: set sw=8: */

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
#include "format.h"
#include "command-context.h"
#include "workbook-control.h"
#include "workbook-view.h"
#include "workbook.h"
#include "ranges.h"
#include "sort.h"
#include "eval.h"
#include "expr.h"
#include "cell.h"
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

#define MAX_DESCRIPTOR_WIDTH 15

/*
 * There are several distinct stages to wrapping each command.
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
	int size;                       /* See truncate_undo_info.  */
} GnumericCommand;

typedef gboolean (* UndoCmd)(GnumericCommand *this, WorkbookControl *wbc);
typedef gboolean (* RedoCmd)(GnumericCommand *this, WorkbookControl *wbc);

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

#define GNUMERIC_MAKE_COMMAND(type, func)				\
static gboolean								\
func ## _undo (GnumericCommand *me, WorkbookControl *wbc);		\
static gboolean								\
func ## _redo (GnumericCommand *me, WorkbookControl *wbc);		\
static void								\
func ## _destroy (GtkObject *object);					\
static void								\
func ## _class_init (GnumericCommandClass * const parent)		\
{									\
	parent->undo_cmd = (UndoCmd)& func ## _undo;			\
	parent->redo_cmd = (RedoCmd)& func ## _redo;			\
	if (gtk_object_dtor == NULL)					\
		gtk_object_dtor = parent->parent_class.destroy;		\
	parent->parent_class.destroy = & func ## _destroy;		\
}									\
static GNUMERIC_MAKE_TYPE_WITH_CLASS(func, #type, type, GnumericCommandClass, func ## _class_init, NULL, \
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
	char const *undo_label = get_menu_label (wb->undo_commands);
	char const *redo_label = get_menu_label (wb->redo_commands);

	WORKBOOK_FOREACH_CONTROL (wb, view, control,
		wb_control_undo_redo_labels (control, undo_label, redo_label);
	);
}

/*
 * command_undo : Undo the last command executed.
 *
 * @wbc : The workbook control which issued the request.
 *        Any user level errors generated by undoing will be reported
 *        here.
 *
 * @wb : The workbook whose commands to undo.
 */
void
command_undo (WorkbookControl *wbc)
{
	GnumericCommand *cmd;
	GnumericCommandClass *klass;
	Workbook *wb = wb_control_workbook (wbc);

	g_return_if_fail (wb != NULL);
	g_return_if_fail (wb->undo_commands != NULL);

	cmd = GNUMERIC_COMMAND (wb->undo_commands->data);
	g_return_if_fail (cmd != NULL);

	klass = GNUMERIC_COMMAND_CLASS (cmd->parent.klass);
	g_return_if_fail (klass != NULL);

	/* TRUE indicates a failure to undo.  Leave the command where it is */
	if (klass->undo_cmd (cmd, wbc))
		return;

	wb->undo_commands = g_slist_remove (wb->undo_commands,
					    wb->undo_commands->data);
	wb->redo_commands = g_slist_prepend (wb->redo_commands, cmd);

	WORKBOOK_FOREACH_CONTROL (wb, view, control,
	{
		wb_control_undo_redo_pop (control, TRUE);
		wb_control_undo_redo_push (control, cmd->cmd_descriptor, FALSE);
	});
	undo_redo_menu_labels (wb);
	/* TODO : Should we mark the workbook as clean or pristine too */
}

/*
 * command_redo : Redo the last command that was undone.
 *
 * @wbc : The workbook control which issued the request.
 *        Any user level errors generated by redoing will be reported
 *        here.
 */
void
command_redo (WorkbookControl *wbc)
{
	GnumericCommand *cmd;
	GnumericCommandClass *klass;
	Workbook *wb = wb_control_workbook (wbc);

	g_return_if_fail (wb);
	g_return_if_fail (wb->redo_commands);

	cmd = GNUMERIC_COMMAND (wb->redo_commands->data);
	g_return_if_fail (cmd != NULL);

	klass = GNUMERIC_COMMAND_CLASS (cmd->parent.klass);
	g_return_if_fail (klass != NULL);

	/* TRUE indicates a failure to redo.  Leave the command where it is */
	if (klass->redo_cmd (cmd, wbc))
		return;

	/* Remove the command from the undo list */
	wb->redo_commands = g_slist_remove (wb->redo_commands,
					    wb->redo_commands->data);
	wb->undo_commands = g_slist_prepend (wb->undo_commands, cmd);

	WORKBOOK_FOREACH_CONTROL (wb, view, control,
	{
		wb_control_undo_redo_push (control,
					   cmd->cmd_descriptor, TRUE);
		wb_control_undo_redo_pop (control, FALSE);
	});
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

/*
 * Each undo item has a certain size.  The size of typing a value into
 * a cell is the unit size.  A large autoformat could have a size of
 * hundreds or even thousands.
 *
 * We wish to have the same undo behaviour across platforms, so please
 * don't use sizeof in computing the undo size.
 */

#undef DEBUG_TRUNCATE_UNDO

/*
 * Truncate the undo list if it is too big.
 *
 * Returns -1 if no truncation was done, or else the number of elements
 * left.
 */
static int
truncate_undo_info (WorkbookControl *wbc)
{
	int size_left = 100; /* FIXME? */
	int ok_count;
	GSList *l, *prev;
	Workbook *wb = wb_control_workbook (wbc);

#ifdef DEBUG_TRUNCATE_UNDO
	fprintf (stderr, "Undo sizes:");
#endif

	for (l = wb->undo_commands, prev = NULL, ok_count = 0;
	     l;
	     prev = l, l = l->next, ok_count++) {
		int min_leave;
		GnumericCommand *cmd = GNUMERIC_COMMAND (l->data);
		int size = cmd->size;

		if (size < 1) {
				/*
				 * We could g_assert, but that would cause data loss.
				 * Instead, just continue.
				 */
			g_warning ("Faulty undo_size_func, please report.");
			size = 1;
		}

#ifdef DEBUG_TRUNCATE_UNDO
			fprintf (stderr, " %d", size);
#endif

		/* Keep at least one undo item.  */
		if (size > size_left && ok_count >= 1) {
			/* Current item is too big; truncate list here.  */
			command_list_release (l);
			prev->next = NULL;
#ifdef DEBUG_TRUNCATE_UNDO
			fprintf (stderr, "[trunc]\n");
#endif
			return ok_count;
		}

		/*
		 * In order to allow a series of useful small items behind
		 * a big item, leave at least 10% of current item's size.
		 */
		min_leave = size / 10;
		size_left = MAX (size_left - size, min_leave);
	}

#ifdef DEBUG_TRUNCATE_UNDO
	fprintf (stderr, "\n");
#endif
	return -1;
}


/**
 * command_push_undo : An internal utility to tack a new command
 *    onto the undo list.
 *
 * @wbc : The workbook control that issued the command.
 * @cmd : The new command to add.
 *
 * returns : TRUE if there was an error.
 */
static gboolean
command_push_undo (WorkbookControl *wbc, GtkObject *obj)
{
	gboolean trouble;
	GnumericCommand *cmd;
	GnumericCommandClass *klass;
	Workbook *wb = wb_control_workbook (wbc);

	g_return_val_if_fail (wbc != NULL, TRUE);

	cmd = GNUMERIC_COMMAND (obj);
	g_return_val_if_fail (cmd != NULL, TRUE);

	klass = GNUMERIC_COMMAND_CLASS (cmd->parent.klass);
	g_return_val_if_fail (klass != NULL, TRUE);

	/* TRUE indicates a failure to do the command */
	trouble = klass->redo_cmd (cmd, wbc);

	if  (!trouble) {
		int undo_trunc;

		command_list_release (wb->redo_commands);
		wb->redo_commands = NULL;

		wb->undo_commands = g_slist_prepend (wb->undo_commands, cmd);
		undo_trunc = truncate_undo_info (wbc);

		WORKBOOK_FOREACH_CONTROL (wb, view, control,
		{
			wb_control_undo_redo_push (control,
						   cmd->cmd_descriptor, TRUE);
			if (undo_trunc >= 0)
				wb_control_undo_redo_truncate (control, undo_trunc, TRUE);
			wb_control_undo_redo_clear (control, FALSE);
				
		});
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
cmd_set_text_undo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	CmdSetText *me = CMD_SET_TEXT (cmd);
	Cell *cell;
	char *new_text;

	g_return_val_if_fail (me != NULL, TRUE);

	/* Get the cell */
	cell = sheet_cell_get (me->pos.sheet,
			       me->pos.eval.col,
			       me->pos.eval.row);

	/* Save the new value so we can redo */
	new_text = cell_is_blank (cell) ? NULL : cell_get_entered_text (cell);

	/* Restore the old value if it was not empty */
	if (me->text != NULL) {
		if (cell == NULL)
			cell = sheet_cell_new (me->pos.sheet,
					       me->pos.eval.col,
					       me->pos.eval.row);
		sheet_cell_set_text (cell, me->text);
		g_free (me->text);
	} else if (cell != NULL)
		sheet_clear_region (wbc, me->pos.sheet,
				    me->pos.eval.col, me->pos.eval.row,
				    me->pos.eval.col, me->pos.eval.row,
				    CLEAR_VALUES);

	me->text = new_text;

	sheet_set_dirty (me->pos.sheet, TRUE);
	workbook_recalc (me->pos.sheet->workbook);
	sheet_update (me->pos.sheet);

	return FALSE;
}

static gboolean
cmd_set_text_redo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	/* Undo and redo are the same for this case */
	return cmd_set_text_undo (cmd, wbc);
}

static void
cmd_set_text_destroy (GtkObject *cmd)
{
	CmdSetText *me = CMD_SET_TEXT (cmd);
	if (me->text != NULL) {
		g_free (me->text);
		me->text = NULL;
	}
	gnumeric_command_destroy (cmd);
}

gboolean
cmd_set_text (WorkbookControl *wbc,
	      Sheet *sheet, CellPos const *pos,
	      const char *new_text)
{
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
		gnumeric_error_splits_array (COMMAND_CONTEXT (wbc),
					     _("Set Text"));
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
	if (strlen (corrected_text) > MAX_DESCRIPTOR_WIDTH) {
		pad = "..."; /* length of 3 */
		text = g_strndup (corrected_text,
				  MAX_DESCRIPTOR_WIDTH - 3);
	} else
		text = corrected_text;

	me->parent.size = 1;

	me->parent.cmd_descriptor =
		g_strdup_printf (_("Typing \"%s%s\" in %s"), text, pad,
				 cell_pos_name (pos));

	if (text != corrected_text)
		g_free (text);

	/* Register the command object */
	return command_push_undo (wbc, obj);
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
cmd_area_set_text_undo (GnumericCommand *cmd, WorkbookControl *wbc)
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
		clipboard_paste_region (wbc,
					paste_target_init (&pt, me->pos.sheet, r, PASTE_CONTENT),
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
cmd_area_set_text_redo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	CmdAreaSetText *me = CMD_AREA_SET_TEXT (cmd);
	ExprTree *expr = NULL;
	GSList *l;
	char const *start;

	g_return_val_if_fail (me != NULL, TRUE);

	/* Check for array subdivision */
	if (selection_check_for_array (me->pos.sheet, me->selection)) {
		gnumeric_error_splits_array (COMMAND_CONTEXT (wbc), _("Set Text"));
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
		GList *deps;
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

		/* mark content as dirty */
		sheet_flag_status_update_range (me->pos.sheet, r);
		deps = sheet_region_get_deps (me->pos.sheet, r);
		if (deps)
			dependent_queue_recalc_list (deps, TRUE);
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
cmd_area_set_text (WorkbookControl *wbc, EvalPos const *pos,
		   char const *new_text, gboolean as_array)
{
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

	if (strlen (new_text) > MAX_DESCRIPTOR_WIDTH) {
		pad = "..."; /* length of 3 */
		text = g_strndup (new_text,
				  MAX_DESCRIPTOR_WIDTH - 3);
	} else
		text = (gchar *) new_text;

	me->parent.size = 1;

	me->parent.cmd_descriptor =
	    g_strdup_printf (_("Typing \"%s%s\""), text, pad);

	if (*pad)
		g_free (text);

	/* Register the command object */
	return command_push_undo (wbc, obj);
}

/******************************************************************/

#define CMD_INS_DEL_COLROW_TYPE        (cmd_ins_del_colrow_get_type ())
#define CMD_INS_DEL_COLROW(o)          (GTK_CHECK_CAST ((o), CMD_INS_DEL_COLROW_TYPE, CmdInsDelColRow))

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
} CmdInsDelColRow;

GNUMERIC_MAKE_COMMAND (CmdInsDelColRow, cmd_ins_del_colrow);

static gboolean
cmd_ins_del_colrow_undo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	CmdInsDelColRow *me = CMD_INS_DEL_COLROW (cmd);
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
			trouble = sheet_insert_cols (wbc, me->sheet, me->index, me->count, &tmp);
		else
			trouble = sheet_insert_rows (wbc, me->sheet, me->index, me->count, &tmp);
	} else {
		index = ((me->is_cols) ? SHEET_MAX_COLS : SHEET_MAX_ROWS) - me->count;
		if (me->is_cols)
			trouble = sheet_delete_cols (wbc, me->sheet, me->index, me->count, &tmp);
		else
			trouble = sheet_delete_rows (wbc, me->sheet, me->index, me->count, &tmp);
	}

	/* restore row/col sizes */
	colrow_restore_sizes (me->sheet, me->is_cols, index, index + me->count - 1, me->sizes);
	me->sizes = NULL;

	/* restore row/col contents */
	if (me->is_cols)
		range_init (&r, index, 0, index+me->count-1, SHEET_MAX_ROWS-1);
	else
		range_init (&r, 0, index, SHEET_MAX_COLS-1, index+me->count-1);

	clipboard_paste_region (wbc,
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

	/* Ins/Del Row/Col unants things, and clears the cut selection */
	application_clipboard_unant ();
	if (NULL == application_clipboard_contents_get ())
		application_clipboard_clear (TRUE);


	return trouble;
}

static gboolean
cmd_ins_del_colrow_redo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	CmdInsDelColRow *me = CMD_INS_DEL_COLROW (cmd);
	Range r;
	gboolean trouble;
	int first, last;

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->sizes == NULL, TRUE);
	g_return_val_if_fail (me->contents == NULL, TRUE);

	first = (me->is_insert)
	    ? (((me->is_cols) ? SHEET_MAX_COLS : SHEET_MAX_ROWS) - me->count)
	    : me->index;

	last = first + me->count - 1;
	me->sizes = colrow_save_sizes (me->sheet, me->is_cols, first, last);
	me->contents = clipboard_copy_range (me->sheet,
		(me->is_cols)
		? range_init (&r, first, 0, last, SHEET_MAX_ROWS - 1)
		: range_init (&r, 0, first, SHEET_MAX_COLS-1, last));

	if (me->is_insert) {
		if (me->is_cols)
			trouble = sheet_insert_cols (wbc, me->sheet, me->index,
						     me->count, &me->reloc_storage);
		else
			trouble = sheet_insert_rows (wbc, me->sheet, me->index,
						     me->count, &me->reloc_storage);
	} else {
		if (me->is_cols)
			trouble = sheet_delete_cols (wbc, me->sheet, me->index,
						     me->count, &me->reloc_storage);
		else
			trouble = sheet_delete_rows (wbc, me->sheet, me->index,
						     me->count, &me->reloc_storage);
	}

	sheet_set_dirty (me->sheet, TRUE);
	workbook_recalc (me->sheet->workbook);
	sheet_update (me->sheet);

	/* Ins/Del Row/Col unants things, and clears the cut selection */
	application_clipboard_unant ();
	if (NULL == application_clipboard_contents_get ())
		application_clipboard_clear (TRUE);

	return trouble;
}

static void
cmd_ins_del_colrow_destroy (GtkObject *cmd)
{
	CmdInsDelColRow *me = CMD_INS_DEL_COLROW (cmd);

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
cmd_ins_del_colrow (WorkbookControl *wbc,
		     Sheet *sheet,
		     gboolean is_cols, gboolean is_insert,
		     char const * descriptor, int index, int count)
{
	GtkObject *obj;
	CmdInsDelColRow *me;

	g_return_val_if_fail (sheet != NULL, TRUE);

	obj = gtk_type_new (CMD_INS_DEL_COLROW_TYPE);
	me = CMD_INS_DEL_COLROW (obj);

	/* Store the specs for the object */
	me->sheet = sheet;
	me->is_cols = is_cols;
	me->is_insert = is_insert;
	me->index = index;
	me->count = count;
	me->sizes = NULL;
	me->contents = NULL;

	me->parent.size = 1;  /* FIXME?  */

	me->parent.cmd_descriptor = descriptor;

	/* Register the command object */
	return command_push_undo (wbc, obj);
}

gboolean
cmd_insert_cols (WorkbookControl *wbc,
		 Sheet *sheet, int start_col, int count)
{
	char *mesg = g_strdup_printf ((count > 1)
				      ? _("Inserting %d columns before %s")
				      : _("Inserting %d column before %s"), count,
				      col_name (start_col));
	return cmd_ins_del_colrow (wbc, sheet, TRUE, TRUE, mesg,
				   start_col, count);
}

gboolean
cmd_insert_rows (WorkbookControl *wbc,
		 Sheet *sheet, int start_row, int count)
{
	char *mesg = g_strdup_printf ((count > 1)
				      ? _("Inserting %d rows before %d")
				      : _("Inserting %d row before %d"),
				      count, start_row+1);
	return cmd_ins_del_colrow (wbc, sheet, FALSE, TRUE, mesg,
				   start_row, count);
}

gboolean
cmd_delete_cols (WorkbookControl *wbc,
		 Sheet *sheet, int start_col, int count)
{
	char *mesg;
	if (count > 1) {
		/* col_name uses a static buffer */
		char *temp = g_strdup_printf (_("Deleting %d columns %s:"),
					      count, col_name (start_col));
		mesg = g_strconcat (temp, col_name (start_col+count-1), NULL);
		g_free (temp);
	} else
		mesg = g_strdup_printf (_("Deleting column %s"),
					col_name (start_col));

	return cmd_ins_del_colrow (wbc, sheet, TRUE, FALSE, mesg, start_col, count);
}

gboolean
cmd_delete_rows (WorkbookControl *wbc,
		 Sheet *sheet, int start_row, int count)
{
	char *mesg = (count > 1)
	    ? g_strdup_printf (_("Deleting %d rows %d:%d"), count, start_row,
			       start_row+count-1)
	    : g_strdup_printf (_("Deleting row %d"), start_row);

	return cmd_ins_del_colrow (wbc, sheet, FALSE, FALSE, mesg, start_row, count);
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
cmd_clear_undo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	CmdClear *me = CMD_CLEAR (cmd);
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
		clipboard_paste_region (wbc,
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
cmd_clear_redo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	CmdClear *me = CMD_CLEAR (cmd);
	GSList *l;

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->selection != NULL, TRUE);
	g_return_val_if_fail (me->old_content == NULL, TRUE);

	/* Check for array subdivision */
	if (selection_check_for_array (me->sheet, me->selection)) {
		gnumeric_error_splits_array (COMMAND_CONTEXT (wbc), _("Undo Clear"));
		return TRUE;
	}

	for (l = me->selection ; l != NULL ; l = l->next) {
		Range const * const r = l->data;
		me->old_content =
			g_slist_prepend (me->old_content,
				clipboard_copy_range (me->sheet, r));

		/* We have already checked the arrays */
		sheet_clear_region (wbc, me->sheet,
				    r->start.col, r->start.row,
				    r->end.col, r->end.row,
				    me->clear_flags|CLEAR_NOCHECKARRAY);
	}
	me->old_content = g_slist_reverse (me->old_content);

	sheet_set_dirty (me->sheet, TRUE);
	workbook_recalc (me->sheet->workbook);
	sheet_update (me->sheet);

	return FALSE;
}

static void
cmd_clear_destroy (GtkObject *cmd)
{
	CmdClear *me = CMD_CLEAR (cmd);

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
cmd_clear_selection (WorkbookControl *wbc, Sheet *sheet, int clear_flags)
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
		me->paste_flags |= PASTE_CONTENT;
	if (clear_flags & CLEAR_FORMATS)
		me->paste_flags |= PASTE_FORMATS;
	if (clear_flags & CLEAR_COMMENTS)
		g_warning ("Deleted comments cannot be restored yet");

	me->parent.size = 1;  /* FIXME?  */

	/* TODO : Something more descriptive ? maybe the range name */
	me->parent.cmd_descriptor = g_strdup (_("Clear"));

	/* Register the command object */
	return command_push_undo (wbc, obj);
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
	StyleBorder  **borders;
} CmdFormat;

GNUMERIC_MAKE_COMMAND (CmdFormat, cmd_format);

static gboolean
cmd_format_undo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	CmdFormat *me = CMD_FORMAT (cmd);

	g_return_val_if_fail (me != NULL, TRUE);

	if (me->old_styles) {
		GSList *l1 = me->old_styles;
		GSList *l2 = me->selection;

		for (; l1; l1 = l1->next, l2 = l2->next) {
			Range const *r;
			CmdFormatOldStyle *os = l1->data;
			SpanCalcFlags flags =
				sheet_style_attach_list (me->sheet, os->styles,
							 &os->pos, FALSE);

			g_return_val_if_fail (l2 && l2->data, TRUE);

			r = l2->data;
			sheet_range_calc_spans (me->sheet, *r, flags);
			if (flags != SPANCALC_SIMPLE)
				rows_height_update (me->sheet, r);
			sheet_flag_format_update_range (me->sheet, r);
		}
	}

	sheet_set_dirty (me->sheet, TRUE);
	workbook_recalc (me->sheet->workbook);
	sheet_update (me->sheet);

	return FALSE;
}

static gboolean
cmd_format_redo (GnumericCommand *cmd, WorkbookControl *wbc)
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
			sheet_style_apply_range (me->sheet, l->data,
						 me->new_style);
		}
		sheet_flag_format_update_range (me->sheet, l->data);
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
 * @wbc: the workbook control.
 * @sheet: the sheet
 * @style: style to apply to the selection
 * @borders: borders to apply to the selection
 *
 * If borders is non NULL, then the StyleBorder references are passed,
 * the MStyle reference is also passed.
 *
 * Return value: TRUE if there was a problem
 **/
gboolean
cmd_format (WorkbookControl *wbc, Sheet *sheet,
	    MStyle *style, StyleBorder **borders)
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

	me->parent.size = 1;  /* Updated below.  */

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

		me->parent.size += g_list_length (os->styles);
		me->old_styles = g_slist_append (me->old_styles, os);
	}

	if (borders) {
		int i;

		me->borders = g_new (StyleBorder *, STYLE_BORDER_EDGE_MAX);
		for (i = STYLE_BORDER_TOP; i < STYLE_BORDER_EDGE_MAX; i++)
			me->borders [i] = borders [i];
	} else
		me->borders = NULL;

	me->parent.cmd_descriptor = g_strdup (_("Format cells"));

	/* Register the command object */
	return command_push_undo (wbc, obj);
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
cmd_rename_sheet_undo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	CmdRenameSheet *me = CMD_RENAME_SHEET (cmd);

	g_return_val_if_fail (me != NULL, TRUE);

	return workbook_sheet_rename (wbc, me->wb,
				      me->new_name, me->old_name);
}

static gboolean
cmd_rename_sheet_redo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	CmdRenameSheet *me = CMD_RENAME_SHEET (cmd);

	g_return_val_if_fail (me != NULL, TRUE);

	return workbook_sheet_rename (wbc, me->wb,
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
cmd_rename_sheet (WorkbookControl *wbc, const char *old_name, const char *new_name)
{
	GtkObject *obj;
	CmdRenameSheet *me;
	Workbook *wb = wb_control_workbook (wbc);

	g_return_val_if_fail (wb != NULL, TRUE);

	obj = gtk_type_new (CMD_RENAME_SHEET_TYPE);
	me = CMD_RENAME_SHEET (obj);

	/* Store the specs for the object */
	me->wb = wb;
	me->old_name = g_strdup (old_name);
	me->new_name = g_strdup (new_name);

	me->parent.size = 1;

	me->parent.cmd_descriptor =
	    g_strdup_printf (_("Rename sheet '%s' '%s'"), old_name, new_name);

	/* Register the command object */
	return command_push_undo (wbc, obj);
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
cmd_set_date_time_undo (GnumericCommand *cmd, WorkbookControl *wbc)
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
		sheet_clear_region (wbc, me->pos.sheet,
				    me->pos.eval.col, me->pos.eval.row,
				    me->pos.eval.col, me->pos.eval.row,
				    CLEAR_VALUES);

	sheet_set_dirty (sheet, TRUE);
	workbook_recalc (sheet->workbook);
	sheet_update (sheet);

	return FALSE;
}

static gboolean
cmd_set_date_time_redo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	CmdSetDateTime *me = CMD_SET_DATE_TIME (cmd);
	Value *v;
	Cell *cell;
	StyleFormat *prefered_format;

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
		prefered_format = style_format_new_XL (_(">mm/dd/yyyy") + 1, TRUE);
	} else {
		v = value_new_float (datetime_timet_to_seconds (time (NULL)) / (24.0 * 60 * 60));

		/* FIXME : See comment above */
		prefered_format = style_format_new_XL (_(">hh:mm") + 1, TRUE);
	}

	/* Get the cell (creating it if needed) */
	cell = sheet_cell_fetch (me->pos.sheet, me->pos.eval.col, me->pos.eval.row);

	/* Save contents */
	me->contents = (cell->value) ? cell_get_entered_text (cell) : NULL;

	sheet_cell_set_value (cell, v, prefered_format);
	style_format_unref (prefered_format);

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
cmd_set_date_time (WorkbookControl *wbc,
		   Sheet *sheet, CellPos const *pos, gboolean is_date)
{
	GtkObject *obj;
	CmdSetDateTime *me;
	Cell const *cell;

	g_return_val_if_fail (sheet != NULL, TRUE);

	/* Ensure that we are not splitting up an array */
	cell = sheet_cell_get (sheet, pos->col, pos->row);
	if (cell_is_partial_array (cell)) {
		gnumeric_error_splits_array (COMMAND_CONTEXT (wbc),
					     _("Set Date/Time"));
		return TRUE;
	}

	obj = gtk_type_new (CMD_SET_DATE_TIME_TYPE);
	me = CMD_SET_DATE_TIME (obj);

	/* Store the specs for the object */
	me->pos.sheet = sheet;
	me->pos.eval = *pos;
	me->is_date = is_date;
	me->contents = NULL;

	me->parent.size = 1;

	me->parent.cmd_descriptor =
	    g_strdup_printf (is_date
			     ? _("Setting current date in %s")
			     : _("Setting current time in %s"),
			     cell_coord_name (pos->col, pos->row));

	/* Register the command object */
	return command_push_undo (wbc, obj);
}

/******************************************************************/

#define CMD_RESIZE_COLROW_TYPE        (cmd_resize_colrow_get_type ())
#define CMD_RESIZE_COLROW(o)          (GTK_CHECK_CAST ((o), CMD_RESIZE_COLROW_TYPE, CmdResizeColRow))

typedef struct
{
	GnumericCommand parent;

	Sheet		*sheet;
	gboolean	 is_cols;
	ColRowIndexList *selection;
	ColRowSizeList	*saved_sizes;
	int		 new_size;
} CmdResizeColRow;

GNUMERIC_MAKE_COMMAND (CmdResizeColRow, cmd_resize_colrow);

static gboolean
cmd_resize_colrow_undo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	CmdResizeColRow *me = CMD_RESIZE_COLROW (cmd);

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->selection != NULL, TRUE);
	g_return_val_if_fail (me->saved_sizes != NULL, TRUE);

	colrow_restore_sizes_group (me->sheet, me->is_cols,
				    me->selection, me->saved_sizes,
				    me->new_size);
	me->saved_sizes = NULL;

	sheet_set_dirty (me->sheet, TRUE);
	sheet_update (me->sheet);

	return FALSE;
}

static gboolean
cmd_resize_colrow_redo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	CmdResizeColRow *me = CMD_RESIZE_COLROW (cmd);

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->selection != NULL, TRUE);
	g_return_val_if_fail (me->saved_sizes == NULL, TRUE);

	me->saved_sizes = colrow_set_sizes (me->sheet, me->is_cols,
					    me->selection, me->new_size);
	if (me->parent.size == 1)
		me->parent.size += (g_slist_length (me->saved_sizes) +
				    g_list_length (me->selection));

	sheet_set_dirty (me->sheet, TRUE);
	sheet_update (me->sheet);

	return FALSE;
}
static void
cmd_resize_colrow_destroy (GtkObject *cmd)
{
	CmdResizeColRow *me = CMD_RESIZE_COLROW (cmd);

	if (me->selection)
		me->selection = colrow_index_list_destroy (me->selection);

	if (me->saved_sizes)
		me->saved_sizes = colrow_size_list_destroy (me->saved_sizes);

	gnumeric_command_destroy (cmd);
}

gboolean
cmd_resize_colrow (WorkbookControl *wbc, Sheet *sheet,
		   gboolean is_cols, ColRowIndexList *selection,
		   int new_size)
{
	GtkObject *obj;
	CmdResizeColRow *me;

	g_return_val_if_fail (sheet != NULL, TRUE);

	obj = gtk_type_new (CMD_RESIZE_COLROW_TYPE);
	me = CMD_RESIZE_COLROW (obj);

	/* Store the specs for the object */
	me->sheet = sheet;
	me->is_cols = is_cols;
	me->selection = selection;
	me->saved_sizes = NULL;
	me->new_size = new_size;

	me->parent.size = 1;  /* Changed in initial redo.  */

	me->parent.cmd_descriptor = is_cols
	    ? g_strdup (_("Setting width of columns"))
	    : g_strdup (_("Setting height of rows"));

	/* Register the command object */
	return command_push_undo (wbc, obj);
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
	if (me->perm != NULL) {
		g_free (me->perm);
		me->perm = NULL;
	}
	if (me->inv != NULL) {
		g_free (me->inv);
		me->inv = NULL;
	}

	gnumeric_command_destroy (cmd);
}

static gboolean
cmd_sort_undo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	CmdSort *me = CMD_SORT (cmd);
	g_return_val_if_fail (me != NULL, TRUE);

	if (!me->inv) {
		me->inv = sort_permute_invert (me->perm, sort_data_length (me->data));
	}
	sort_position (wbc, me->data, me->inv);

	sheet_set_dirty (me->data->sheet, TRUE);
	workbook_recalc (me->data->sheet->workbook);
	sheet_update (me->data->sheet);

	return FALSE;
}

static gboolean
cmd_sort_redo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	CmdSort *me = CMD_SORT (cmd);

	g_return_val_if_fail (me != NULL, TRUE);

	if (!me->perm) {
		me->perm = sort_contents (wbc, me->data);
		me->parent.size += 2 * sort_data_length (me->data);
	} else {
		sort_position (wbc, me->data, me->perm);
	}

	sheet_set_dirty (me->data->sheet, TRUE);
	workbook_recalc (me->data->sheet->workbook);
	sheet_update (me->data->sheet);

	return FALSE;
}

gboolean
cmd_sort (WorkbookControl *wbc, SortData *data)
{
	GtkObject *obj;
	CmdSort *me;

	g_return_val_if_fail (data != NULL, TRUE);

	obj = gtk_type_new (CMD_SORT_TYPE);
	me = CMD_SORT (obj);

	me->data = data;
	me->perm = NULL;
	me->inv = NULL;

	me->parent.size = 1;  /* Changed in initial redo.  */

	me->parent.cmd_descriptor =
		g_strdup_printf (_("Sorting %s"), range_name (me->data->range));

	/* Register the command object */
	return command_push_undo (wbc, obj);
}

/******************************************************************/

#define CMD_HIDE_COLROW_TYPE        (cmd_hide_colrow_get_type ())
#define CMD_HIDE_COLROW(o)          (GTK_CHECK_CAST ((o), CMD_HIDE_COLROW_TYPE, CmdHideColRow))

typedef struct
{
	GnumericCommand parent;

	Sheet         *sheet;
	gboolean       is_cols;
	gboolean       visible;
	ColRowVisList *elements;
} CmdHideColRow;

GNUMERIC_MAKE_COMMAND (CmdHideColRow, cmd_hide_colrow);

static gboolean
cmd_hide_colrow_undo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	CmdHideColRow *me = CMD_HIDE_COLROW (cmd);

	g_return_val_if_fail (me != NULL, TRUE);

	colrow_set_visibility_list (me->sheet, me->is_cols,
				    !me->visible, me->elements);

	sheet_set_dirty (me->sheet, TRUE);
	sheet_update (me->sheet);

	return FALSE;
}

static gboolean
cmd_hide_colrow_redo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	CmdHideColRow *me = CMD_HIDE_COLROW (cmd);

	g_return_val_if_fail (me != NULL, TRUE);

	colrow_set_visibility_list (me->sheet, me->is_cols,
				    me->visible, me->elements);

	sheet_set_dirty (me->sheet, TRUE);
	sheet_update (me->sheet);

	return FALSE;
}

static void
cmd_hide_colrow_destroy (GtkObject *cmd)
{
	CmdHideColRow *me = CMD_HIDE_COLROW (cmd);
	me->elements = colrow_vis_list_destroy (me->elements);
	gnumeric_command_destroy (cmd);
}

gboolean
cmd_hide_selection_colrow (WorkbookControl *wbc, Sheet *sheet,
			   gboolean is_cols, gboolean visible)
{
	GtkObject *obj;
	CmdHideColRow *me;

	g_return_val_if_fail (sheet != NULL, TRUE);

	obj = gtk_type_new (CMD_HIDE_COLROW_TYPE);
	me = CMD_HIDE_COLROW (obj);

	me->sheet = sheet;
	me->is_cols = is_cols;
	me->visible = visible;
	me->elements = colrow_get_visiblity_toggle (sheet, is_cols, visible);

	me->parent.size = 1 + g_slist_length (me->elements);

	me->parent.cmd_descriptor = g_strdup (is_cols
		? (visible ? _("Unhide columns") : _("Hide columns"))
		: (visible ? _("Unhide rows") : _("Hide rows")));

	/* Register the command object */
	return command_push_undo (wbc, obj);
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
	gboolean	 move_selection;
} CmdPasteCut;

GNUMERIC_MAKE_COMMAND (CmdPasteCut, cmd_paste_cut);

typedef struct
{
	PasteTarget pt;
	CellRegion *contents;
} PasteContent;

static gboolean
cmd_paste_cut_undo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	CmdPasteCut *me = CMD_PASTE_CUT (cmd);
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
	sheet_move_range (wbc, &reverse, &tmp);
	workbook_expr_unrelocate_free (tmp);

	while (me->paste_content) {
		PasteContent *pc = me->paste_content->data;
		me->paste_content = g_slist_remove (me->paste_content, pc);

		clipboard_paste_region (wbc, &pc->pt, pc->contents);
		clipboard_release (pc->contents);
		g_free (pc);
	}

	/* Restore the changed expressions */
	workbook_expr_unrelocate (me->info.target_sheet->workbook,
				  me->reloc_storage);
	me->reloc_storage = NULL;

	/* Force update of the status area */
	sheet_flag_status_update_range (me->info.target_sheet, NULL);

	/* Select the original region */
	if (me->move_selection)
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
cmd_paste_cut_redo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	CmdPasteCut *me = CMD_PASTE_CUT (cmd);
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
		g_free (r);
	}

	range_check_sanity (&tmp);

	/* Make sure the destination is selected */
	if (me->move_selection)
		sheet_selection_set (me->info.target_sheet,
				     tmp.start.col, tmp.start.row,
				     tmp.start.col, tmp.start.row,
				     tmp.end.col, tmp.end.row);

	sheet_move_range (wbc, &me->info, &me->reloc_storage);

	sheet_set_dirty (me->info.target_sheet, TRUE);
	workbook_recalc (me->info.target_sheet->workbook);
	sheet_update (me->info.target_sheet);

	return FALSE;
}

static void
cmd_paste_cut_destroy (GtkObject *cmd)
{
	CmdPasteCut *me = CMD_PASTE_CUT (cmd);

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
cmd_paste_cut (WorkbookControl *wbc, ExprRelocateInfo const *info,
	       gboolean move_selection)
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
	me->paste_content  = NULL;
	me->reloc_storage  = NULL;
	me->move_selection = move_selection;

	me->parent.size = 1;  /* FIXME?  */

	me->parent.cmd_descriptor = descriptor;

	/* NOTE : if the destination workbook is different from the source
	 * workbook should we have undo elements in both menus ??  It seems
	 * poor form to hit undo in 1 window and effect another...
	 *
	 * Maybe queue it as two different commands, as a clear in one book
	 * and a paste in the other.  This is not symmetric though.  What
	 * happens to the cells in the original sheet that now reference the
	 * cells in the other?  When do they reset to the original?
	 *
	 * Probably when the clear in the original is undone.
	 */

	/* Register the command object */
	return command_push_undo (wbc, obj);
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
cmd_paste_copy_undo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	CmdPasteCopy *me = CMD_PASTE_COPY (cmd);
	CellRegion *content;

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->content != NULL, TRUE);

	content = clipboard_copy_range (me->dst.sheet, &me->dst.range);
	if (clipboard_paste_region (wbc, &me->dst, me->content)) {
		/* There was a problem, avoid leaking */
		clipboard_release (content);
		return TRUE;
	}

	if (me->has_been_through_cycle)
		clipboard_release (me->content);
	else
		/* Save the content */
		me->dst.paste_flags = PASTE_CONTENT |
			(me->dst.paste_flags & PASTE_FORMATS);

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
cmd_paste_copy_redo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	return cmd_paste_copy_undo (cmd, wbc);
}
static void
cmd_paste_copy_destroy (GtkObject *cmd)
{
	CmdPasteCopy *me = CMD_PASTE_COPY (cmd);

	if (me->content) {
		if (me->has_been_through_cycle)
			clipboard_release (me->content);
		me->content = NULL;
	}
	gnumeric_command_destroy (cmd);
}

gboolean
cmd_paste_copy (WorkbookControl *wbc,
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

	me->parent.size = 1;  /* FIXME?  */

	me->parent.cmd_descriptor = g_strdup_printf (_("Pasting into %s"),
						     range_name (&pt->range));

	/* Register the command object */
	return command_push_undo (wbc, obj);
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
cmd_autofill_undo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	CmdAutofill *me = CMD_AUTOFILL (cmd);
	gboolean res;

	g_return_val_if_fail (wbc != NULL, TRUE);
	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->content != NULL, TRUE);

	res = clipboard_paste_region (wbc, &me->dst, me->content);
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
cmd_autofill_redo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	GList *deps;
	CmdAutofill *me = CMD_AUTOFILL (cmd);

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->content == NULL, TRUE);

	me->content = clipboard_copy_range (me->dst.sheet, &me->dst.range);
	if (me->parent.size == 1)
		me->parent.size += (g_list_length (me->content->list) +
				    g_list_length (me->content->styles) +
				    1);
	sheet_autofill (me->dst.sheet,
			me->base_col, me->base_row, me->w, me->h,
			me->end_col, me->end_row);

	/* Make the newly filled content the selection (this queues a redraw) */
	sheet_selection_reset_only (me->dst.sheet);
	sheet_selection_add_range (me->dst.sheet,
				   me->base_col, me->base_row,
				   me->base_col, me->base_row,
				   me->end_col, me->end_row);

	deps = sheet_region_get_deps (me->dst.sheet, &me->dst.range);
	if (deps)
		dependent_queue_recalc_list (deps, TRUE);
	sheet_range_calc_spans (me->dst.sheet, me->dst.range, SPANCALC_RENDER);
	sheet_flag_status_update_range (me->dst.sheet, &me->dst.range);

	sheet_set_dirty (me->dst.sheet, TRUE);
	workbook_recalc (me->dst.sheet->workbook);
	sheet_update (me->dst.sheet);

	return FALSE;
}

static void
cmd_autofill_destroy (GtkObject *cmd)
{
	CmdAutofill *me = CMD_AUTOFILL (cmd);

	if (me->content) {
		clipboard_release (me->content);
		me->content = NULL;
	}
	gnumeric_command_destroy (cmd);
}

gboolean
cmd_autofill (WorkbookControl *wbc, Sheet *sheet,
	      int base_col, int base_row,
	      int w, int h, int end_col, int end_row)
{
	GtkObject *obj;
	CmdAutofill *me;

	/* This would be meaningless */
	if (base_col+w-1 == end_col && base_row+h-1 == end_row)
		return FALSE;

	g_return_val_if_fail (sheet != NULL, TRUE);

	obj = gtk_type_new (CMD_AUTOFILL_TYPE);
	me = CMD_AUTOFILL (obj);

	/* Store the specs for the object */
	me->content = NULL;
	me->dst.sheet = sheet;
	me->dst.paste_flags = PASTE_CONTENT | PASTE_FORMATS;

	/* FIXME : We can copy less than this */
	range_init (&me->dst.range, base_col, base_row, end_col, end_row);
	me->base_col = base_col;
	me->base_row = base_row,
	me->w = w;
	me->h = h;
	me->end_col = end_col;
	me->end_row = end_row;

	me->parent.size = 1;  /* Changed in initial redo.  */

	me->parent.cmd_descriptor = g_strdup (_("Autofill"));

	/* Register the command object */
	return command_push_undo (wbc, obj);
}

/******************************************************************/

#define CMD_AUTOFORMAT_TYPE        (cmd_autoformat_get_type ())
#define CMD_AUTOFORMAT(o)          (GTK_CHECK_CAST ((o), CMD_AUTOFORMAT_TYPE, CmdAutoFormat))

typedef struct {
	CellPos pos;
	GList  *styles;
} CmdAutoFormatOldStyle;

typedef struct {
	GnumericCommand parent;

	Sheet          *sheet;

	GSList         *selections;  /* Selections on the sheet */
	GSList         *old_styles;  /* Older styles, one style_list per selection range*/

	FormatTemplate *ft;         /* Template that has been applied */
} CmdAutoFormat;

GNUMERIC_MAKE_COMMAND (CmdAutoFormat, cmd_autoformat);

static gboolean
cmd_autoformat_undo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	CmdAutoFormat *me = CMD_AUTOFORMAT (cmd);

	g_return_val_if_fail (me != NULL, TRUE);

	if (me->old_styles) {
		GSList *l1 = me->old_styles;
		GSList *l2 = me->selections;

		for (; l1; l1 = l1->next, l2 = l2->next) {
			Range *r;
			CmdAutoFormatOldStyle *os = l1->data;
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
cmd_autoformat_redo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	CmdAutoFormat *me = CMD_AUTOFORMAT (cmd);

	g_return_val_if_fail (me != NULL, TRUE);

	format_template_apply_to_sheet_regions (me->ft, me->sheet, me->selections);

	sheet_set_dirty (me->sheet, TRUE);
	workbook_recalc (me->sheet->workbook);
	sheet_update (me->sheet);

	return FALSE;
}

static void
cmd_autoformat_destroy (GtkObject *cmd)
{
	CmdAutoFormat *me = CMD_AUTOFORMAT (cmd);

	if (me->old_styles != NULL) {
		GSList *l;

		for (l = me->old_styles ; l != NULL ; l = g_slist_remove (l, l->data)) {
			CmdAutoFormatOldStyle *os = l->data;

			if (os->styles)
				sheet_style_list_destroy (os->styles);

			g_free (os);
		}

		me->old_styles = NULL;
	}

	if (me->selections != NULL) {
		GSList *l;

		for (l = me->selections ; l != NULL ; l = g_slist_remove (l, l->data))
			g_free (l->data);

		me->selections = NULL;
	}

	format_template_free (me->ft);

	gnumeric_command_destroy (cmd);
}

/**
 * cmd_format:
 * @context: the context.
 * @sheet: the sheet
 * @ft: The format template that was applied
 *
 * Return value: TRUE if there was a problem
 **/
gboolean
cmd_autoformat (WorkbookControl *wbc, Sheet *sheet, FormatTemplate *ft)
{
	GtkObject *obj;
	CmdAutoFormat *me;
	GSList    *l;

	g_return_val_if_fail (sheet != NULL, TRUE);

	obj = gtk_type_new (CMD_AUTOFORMAT_TYPE);
	me = CMD_AUTOFORMAT (obj);

	me->sheet       = sheet;
	me->selections  = selection_get_ranges (sheet, FALSE); /* Regions may overlap */
	me->ft          = ft;

	me->old_styles = NULL;
	for (l = me->selections; l; l = l->next) {
		CmdFormatOldStyle *os;
		Range range = *((Range const *) l->data);

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

	me->parent.size = 1;  /* FIXME?  */

	me->parent.cmd_descriptor = g_strdup (_("Autoformat"));

	/* Register the command object */
	return command_push_undo (wbc, obj);
}

/******************************************************************/

#define CMD_MERGE_CELLS_TYPE        (cmd_merge_cells_get_type ())
#define CMD_MERGE_CELLS(o)          (GTK_CHECK_CAST ((o), CMD_MERGE_CELLS_TYPE, CmdMergeCells))

typedef struct {
	GnumericCommand parent;

} CmdMergeCells;

GNUMERIC_MAKE_COMMAND (CmdMergeCells, cmd_merge_cells);

static gboolean
cmd_merge_cells_undo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	CmdMergeCells *me = CMD_MERGE_CELLS (cmd);

	g_return_val_if_fail (me != NULL, TRUE);

	return FALSE;
}

static gboolean
cmd_merge_cells_redo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	CmdMergeCells *me = CMD_MERGE_CELLS (cmd);

	g_return_val_if_fail (me != NULL, TRUE);

	return FALSE;
}

static void
cmd_merge_cells_destroy (GtkObject *cmd)
{
	CmdMergeCells *me = CMD_MERGE_CELLS (cmd);

	gnumeric_command_destroy (cmd);
}

/**
 * cmd_merge_cells:
 * @context: the context.
 *
 * Return value: TRUE if there was a problem
 **/
gboolean
cmd_merge_cells (WorkbookControl *wbc, Sheet *sheet, GList *selection)
{
	GtkObject *obj;
	CmdMergeCells *me;
	GSList    *l;

	g_return_val_if_fail (sheet != NULL, TRUE);

	obj = gtk_type_new (CMD_MERGE_CELLS_TYPE);
	me = CMD_MERGE_CELLS (obj);

	me->parent.size = 1;
	me->parent.cmd_descriptor = g_strdup (_("Merge Cells"));

	/* Register the command object */
	return command_push_undo (wbc, obj);
}

/******************************************************************/

#define CMD_UNMERGE_CELLS_TYPE        (cmd_unmerge_cells_get_type ())
#define CMD_UNMERGE_CELLS(o)          (GTK_CHECK_CAST ((o), CMD_UNMERGE_CELLS_TYPE, CmdUnmergeCells))

typedef struct {
	GnumericCommand parent;

	Sheet	*sheet;
	GArray	*regions;
	GArray	*search_ranges;
} CmdUnmergeCells;

GNUMERIC_MAKE_COMMAND (CmdUnmergeCells, cmd_unmerge_cells);

static gboolean
cmd_unmerge_cells_undo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	CmdUnmergeCells *me = CMD_UNMERGE_CELLS (cmd);
	int i;

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->regions != NULL, TRUE);

	for (i = 0 ; i < me->regions->len ; ++i) {
		Range const *tmp = &(g_array_index (me->regions, Range, i));
		sheet_region_merge (COMMAND_CONTEXT (wbc), me->sheet, tmp);
		sheet_range_calc_spans (me->sheet, *tmp, SPANCALC_RE_RENDER);
	}

	g_array_free (me->regions, TRUE);
	me->regions = NULL;

	sheet_set_dirty (me->sheet, TRUE);
	sheet_update (me->sheet);

	return FALSE;
}

static gboolean
cmd_unmerge_cells_redo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	CmdUnmergeCells *me = CMD_UNMERGE_CELLS (cmd);
	int i;

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->regions == NULL, TRUE);

	me->regions = g_array_new (FALSE, FALSE, sizeof (Range));
	for (i = 0 ; i < me->search_ranges->len ; ++i) {
		GSList *ptr, *merged = sheet_region_get_merged (me->sheet,
			&(g_array_index (me->search_ranges, Range, i)));
		for (ptr = merged ; ptr != NULL ; ptr = ptr->next) {
			Range tmp = *(Range *)(ptr->data);
			g_array_append_val (me->regions, tmp);
			sheet_region_unmerge (COMMAND_CONTEXT (wbc),
					      me->sheet, &tmp);
			sheet_range_calc_spans (me->sheet, tmp,
						SPANCALC_RE_RENDER);
		}
		g_slist_free (merged);
	}

	sheet_set_dirty (me->sheet, TRUE);
	sheet_update (me->sheet);

	return FALSE;
}

static void
cmd_unmerge_cells_destroy (GtkObject *cmd)
{
	CmdUnmergeCells *me = CMD_UNMERGE_CELLS (cmd);

	if (me->regions != NULL) {
		g_array_free (me->regions, TRUE);
		me->regions = NULL;
	}
	if (me->search_ranges != NULL) {
		g_array_free (me->search_ranges, TRUE);
		me->search_ranges = NULL;
	}

	gnumeric_command_destroy (cmd);
}

/**
 * cmd_unmerge_cells:
 * @context: the context.
 *
 * Return value: TRUE if there was a problem
 **/
gboolean
cmd_unmerge_cells (WorkbookControl *wbc, Sheet *sheet, GList *selection)
{
	GtkObject *obj;
	CmdUnmergeCells *me;

	g_return_val_if_fail (sheet != NULL, TRUE);

	obj = gtk_type_new (CMD_UNMERGE_CELLS_TYPE);
	me = CMD_UNMERGE_CELLS (obj);

	me->sheet = sheet;
	me->regions = NULL;
	me->search_ranges = g_array_new (FALSE, FALSE, sizeof (Range));
	for ( ; selection != NULL ; selection = selection->next)
		g_array_append_val (me->search_ranges, *(Range *)selection->data);

	me->parent.size = 1;
	me->parent.cmd_descriptor = g_strdup (_("Unmerge Cells"));

	/* Register the command object */
	return command_push_undo (wbc, obj);
}

/******************************************************************/

/* TODO :
 * - SheetObject creation & manipulation.
 */

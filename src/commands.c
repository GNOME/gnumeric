/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * commands.c: Handlers to undo & redo commands
 *
 * Copyright (C) 1999, 2000 Jody Goldberg (jgoldberg@home.com)
 *
 * Contributors : Almer S. Tigelaar (almer@gnome.org)
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
#include "sheet-style.h"
#include "format.h"
#include "command-context.h"
#include "workbook-control.h"
#include "workbook-view.h"
#include "workbook.h"
#include "ranges.h"
#include "sort.h"
#include "eval.h"
#include "value.h"
#include "expr.h"
#include "cell.h"
#include "sheet-merge.h"
#include "parse-util.h"
#include "clipboard.h"
#include "selection.h"
#include "datetime.h"
#include "colrow.h"
#include "dialogs.h"
#include "style-border.h"
#include "dialogs/dialog-autocorrect.h"
#include "sheet-autofill.h"
#include "mstyle.h"
#include "search.h"
#include "sheet-object-cell-comment.h"

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
	Sheet *sheet;			/* primary sheet associated with op */
	int size;                       /* See truncate_undo_info.  */
	char const *cmd_descriptor;	/* A string to put in the menu */
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

#define GNUMERIC_MAKE_COMMAND_WITH_PARENT(type, func, pclass, ptype)	\
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
static GNUMERIC_MAKE_TYPE_WITH_CLASS(func, #type, type, pclass,		\
				     func ## _class_init, NULL, ptype)

#define GNUMERIC_MAKE_COMMAND(type, func)				    \
	GNUMERIC_MAKE_COMMAND_WITH_PARENT(type, func, GnumericCommandClass, \
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

static void
update_after_action (Sheet *sheet)
{
	if (sheet != NULL) {
		g_return_if_fail (IS_SHEET (sheet));

		sheet_set_dirty (sheet, TRUE);
		workbook_recalc (sheet->workbook);
		sheet_update (sheet);

		WORKBOOK_FOREACH_CONTROL (sheet->workbook, view, control,
			  wb_control_sheet_focus (control, sheet);
		);
	}
}

/*
 * range_list_to_string: Convert a list of ranges into a string.
 *                       (The result will be something like :
 *                        "A1:C3, D4:E5"). The string will be
 *                       automatically truncated to MAX_DESCRIPTOR_WIDTH.
 *                       The caller should free the GString that is returned.
 *
 * @ranges : GSList containing Range *'s
 */
static GString *
range_list_to_string (GSList const *ranges)
{
	GString *names;
	GSList const *l;

	g_return_val_if_fail (ranges != NULL, NULL);
	
	names = g_string_new ("");
	for (l = ranges; l != NULL; l = l->next) {
		Range const * const r = l->data;

		/* No need to free range_name, uses static buffer */
		g_string_append (names, range_name (r));

		if (l->next)
			g_string_append (names, ", ");
	}

	/* Make sure the string doesn't get overly wide
	 * There is no need to do this for "types", because that
	 * will never grow indefinitely
	 */
	if (strlen (names->str) > MAX_DESCRIPTOR_WIDTH) {
		g_string_truncate (names, MAX_DESCRIPTOR_WIDTH - 3);
		g_string_append (names, "...");
	}

	return names;
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
	update_after_action (cmd->sheet);

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
	update_after_action (cmd->sheet);

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

/**
 * command_setup_combos :
 * @wbc :
 * 
 * Initialize the combos to correspond to the current undo/redo state.
 */
void
command_setup_combos (WorkbookControl *wbc)
{
	char const *undo_label = NULL, *redo_label = NULL;
	GSList *ptr, *tmp;
	Workbook *wb = wb_control_workbook (wbc);

	g_return_if_fail (wb);

	wb_control_undo_redo_clear (wbc, TRUE);
	tmp = g_slist_reverse (wb->undo_commands);
	for (ptr = tmp ; ptr != NULL ; ptr = ptr->next) {
		undo_label = get_menu_label (ptr);
		wb_control_undo_redo_push (wbc, undo_label, TRUE);
	}
	g_slist_reverse (tmp);

	wb_control_undo_redo_clear (wbc, FALSE);
	tmp = g_slist_reverse (wb->redo_commands);
	for (ptr = tmp ; ptr != NULL ; ptr = ptr->next) {
		redo_label = get_menu_label (ptr);
		wb_control_undo_redo_push (wbc, redo_label, FALSE);
	}
	g_slist_reverse (tmp);

	/* update the menus too */
	wb_control_undo_redo_labels (wbc, undo_label, redo_label);
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
truncate_undo_info (Workbook *wb)
{
	int size_left = 100; /* FIXME? */
	int ok_count;
	GSList *l, *prev;

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
 * command_register_undo : An internal utility to tack a new command
 *    onto the undo list.
 *
 * @wbc : The workbook control that issued the command.
 * @cmd : The new command to add.
 */
static void
command_register_undo (WorkbookControl *wbc, GtkObject *obj)
{
	Workbook *wb;
	GnumericCommand *cmd;
	int undo_trunc;

	g_return_if_fail (wbc != NULL);
	wb = wb_control_workbook (wbc);

	cmd = GNUMERIC_COMMAND (obj);
	g_return_if_fail (cmd != NULL);

	command_list_release (wb->redo_commands);
	wb->redo_commands = NULL;

	wb->undo_commands = g_slist_prepend (wb->undo_commands, cmd);
	undo_trunc = truncate_undo_info (wb);

	WORKBOOK_FOREACH_CONTROL (wb, view, control,
	{
		wb_control_undo_redo_push (control,
					   cmd->cmd_descriptor, TRUE);
		if (undo_trunc >= 0)
			wb_control_undo_redo_truncate (control,
						       undo_trunc, TRUE);
		wb_control_undo_redo_clear (control, FALSE);
	});
	undo_redo_menu_labels (wb);
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

	g_return_val_if_fail (wbc != NULL, TRUE);

	cmd = GNUMERIC_COMMAND (obj);
	g_return_val_if_fail (cmd != NULL, TRUE);

	klass = GNUMERIC_COMMAND_CLASS (cmd->parent.klass);
	g_return_val_if_fail (klass != NULL, TRUE);

	/* TRUE indicates a failure to do the command */
	trouble = klass->redo_cmd (cmd, wbc);
	update_after_action (cmd->sheet);

	if (!trouble)
		command_register_undo (wbc, obj);
	else
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
					     _("Set Text"), NULL);
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

	me->parent.sheet = sheet;
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
		cellregion_free (c);
		me->old_content = g_slist_remove (me->old_content, c);
	}
	g_return_val_if_fail (me->old_content == NULL, TRUE);

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
	if (sheet_ranges_split_region (me->pos.sheet, me->selection,
				       wbc, _("Set Text")))
		return TRUE;

	/*
	 * Only enter an array formula if
	 *   1) the text is a formula
	 *   2) It's entered as an array formula
	 *   3) There is only one 1 selection
	 */
	l = me->selection;
	start = gnumeric_char_start_expr_p (me->text);
	if (start != NULL && me->as_array && l != NULL && l->next == NULL) {
		ParsePos pp;
		expr = expr_parse_string (start,
		    parse_pos_init_evalpos (&pp, &me->pos),
		    NULL, NULL);

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
			cellregion_free (l->data);
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

	me->parent.sheet = pos->sheet;
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
	gboolean         is_cut;
	int		 index;
	int		 count;
	Range           *cutcopied;

	ColRowRLESizeList *sizes;
	CellRegion 	  *contents;
	GSList		  *reloc_storage;
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
	cellregion_free (me->contents);
	me->contents = NULL;

	/* Throw away the undo info for the expressions after the action*/
	workbook_expr_unrelocate_free (tmp);

	/* Restore the changed expressions before the action */
	workbook_expr_unrelocate (me->sheet->workbook, me->reloc_storage);
	me->reloc_storage = NULL;

	/* Ins/Del Row/Col re-ants things completely to account
	 * for the shift of col/rows. 
	 */
	if (me->cutcopied) {
		Range s = *me->cutcopied;

		if (me->is_cut)
			application_clipboard_cut (wbc, me->sheet, &s);
		else
			application_clipboard_copy (wbc, me->sheet, &s);
	}

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
		? colrow_max (me->is_cols) - me->count
		: me->index;

	last = first + me->count - 1;
	me->sizes = colrow_save_sizes (me->sheet, me->is_cols, first, last);
	me->contents = clipboard_copy_range (me->sheet,
		(me->is_cols)
		? range_init (&r, first, 0, last, SHEET_MAX_ROWS - 1)
		: range_init (&r, 0, first, SHEET_MAX_COLS-1, last));

	if (me->is_insert) {
		int prev_visible;

		if (me->is_cols)
			trouble = sheet_insert_cols (wbc, me->sheet, me->index,
						     me->count, &me->reloc_storage);
		else
			trouble = sheet_insert_rows (wbc, me->sheet, me->index,
						     me->count, &me->reloc_storage);

		if (me->index >= 1)
			prev_visible = colrow_find_adjacent_visible (me->sheet, me->is_cols,
								     me->index - 1, FALSE);
		else
			prev_visible = -1;

		/*
		 * We use the size of the column/row to the left/top to determine
		 * the size for the newly inserted column. 
		 * For column 0 we use the default size. And for columns having
		 * the default size we'll just keep the default size aswell.
		 */
		if (prev_visible >= 0) {
			ColRowInfo *info = me->is_cols ? 
				sheet_col_get (me->sheet, prev_visible) :
				sheet_row_get (me->sheet, prev_visible);

			if (info) {
				int i;
				
				for (i = me->index; i < me->index + me->count; i++)
					if (me->is_cols)
						sheet_col_set_size_pixels (me->sheet, i, info->size_pixels,
									   info->hard_size);
					else
						sheet_row_set_size_pixels (me->sheet, i, info->size_pixels,
									   info->hard_size);
			}
		}
	} else {
		if (me->is_cols)
			trouble = sheet_delete_cols (wbc, me->sheet, me->index,
						     me->count, &me->reloc_storage);
		else
			trouble = sheet_delete_rows (wbc, me->sheet, me->index,
						     me->count, &me->reloc_storage);
	}

	/* Ins/Del Row/Col re-ants things completely to account
	 * for the shift of col/rows.
	 */
	if (me->cutcopied) {
		Range s = *me->cutcopied;
		int key = me->is_insert ? me->count : -me->count;
		int threshold = me->is_insert ? me->index : me->index + 1;

		/* Really only applies if the regions that are inserted/
		 * deleted are above the cut/copied region.
		 */
		if (me->is_cols && threshold <= s.start.col) {
			s.start.col += key;
			s.end.col   += key;
		} else if (threshold <= s.start.row) {
			s.start.row += key;
			s.end.row   += key;
		}

		if (me->is_cut)
			application_clipboard_cut (wbc, me->sheet, &s);
		else
			application_clipboard_copy (wbc, me->sheet, &s);
	}

	return trouble;
}

static void
cmd_ins_del_colrow_destroy (GtkObject *cmd)
{
	CmdInsDelColRow *me = CMD_INS_DEL_COLROW (cmd);

	if (me->sizes)
		me->sizes = colrow_rle_size_list_destroy (me->sizes);
	if (me->contents) {
		cellregion_free (me->contents);
		me->contents = NULL;
	}
	if (me->cutcopied)
		g_free (me->cutcopied);
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

	/* We store the cut or/copied range if applicable */	
	if (!application_clipboard_is_empty ()) {
		me->cutcopied = range_dup (application_clipboard_area_get ());
		me->is_cut    = application_clipboard_is_cut ();
	} else
		me->cutcopied = NULL;

	me->parent.sheet = sheet;
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

	/* reset the selection as a convenience AND to queue a redraw */
	sheet_selection_reset (me->sheet);

	for (ranges = me->selection; ranges != NULL ; ranges = ranges->next) {
		Range const * const r = ranges->data;
		CellRegion  *c;
		PasteTarget pt;

		g_return_val_if_fail (me->old_content != NULL, TRUE);

		c = me->old_content->data;

		/* FIXME : no support for pasting comments */
		if (me->clear_flags != CLEAR_COMMENTS)
			clipboard_paste_region (wbc,
				paste_target_init (&pt, me->sheet, r, me->paste_flags),
				c);

		cellregion_free (c);
		me->old_content = g_slist_remove (me->old_content, c);
		sheet_selection_add_range (me->sheet,
			r->start.col, r->start.row,
			r->start.col, r->start.row,
			r->end.col, r->end.row);
	}
	g_return_val_if_fail (me->old_content == NULL, TRUE);

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
	if (sheet_ranges_split_region (me->sheet, me->selection,
				       wbc, _("Undo Clear")))
		return TRUE;

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

	return FALSE;
}

static void
cmd_clear_destroy (GtkObject *cmd)
{
	CmdClear *me = CMD_CLEAR (cmd);

	if (me->old_content != NULL) {
		GSList *l;
		for (l = me->old_content ; l != NULL ; l = g_slist_remove (l, l->data))
			cellregion_free (l->data);
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
	GString *names, *types;
	int paste_flags;

	g_return_val_if_fail (IS_SHEET (sheet), TRUE);

	paste_flags = 0;
	if (clear_flags & CLEAR_VALUES)
		paste_flags |= PASTE_CONTENT;
	if (clear_flags & CLEAR_FORMATS)
		paste_flags |= PASTE_FORMATS;
	if (clear_flags & CLEAR_COMMENTS) {
		static gboolean warn = TRUE;
		if (warn) {
			warn = FALSE;
			g_warning ("Deleted comments cannot be restored yet");
		}
	}

	obj = gtk_type_new (CMD_CLEAR_TYPE);
	me = CMD_CLEAR (obj);

	/* Store the specs for the object */
	me->sheet = sheet;
	me->clear_flags = clear_flags;
	me->paste_flags = paste_flags;
	me->old_content = NULL;
	me->selection = selection_get_ranges (sheet, FALSE /* No intersection */);

	me->parent.sheet = sheet;
	me->parent.size = 1;  /* FIXME?  */

	/* Collect clear types for descriptor */
	if (clear_flags != (CLEAR_VALUES | CLEAR_FORMATS | CLEAR_COMMENTS)) {
		GSList *m, *l = NULL;
		types = g_string_new ("");

		if (clear_flags & CLEAR_VALUES)
			l = g_slist_append (l, g_string_new (_("contents")));
		if (clear_flags & CLEAR_FORMATS)
			l = g_slist_append (l, g_string_new (_("formats")));
		if (clear_flags & CLEAR_COMMENTS)
			l = g_slist_append (l, g_string_new (_("comments")));

		/* Using a list for this may seem overkill, but is really the only
		 * right way to do this
		 */
		for (m = l; m != NULL; m = m->next) {
			GString *s = l->data;
			
			g_string_append (types, s->str);
			g_string_free (s, TRUE);

			if (m->next)
				g_string_append (types, ", ");
		}
		g_slist_free (l);
	} else
		types = g_string_new (_("all"));

	/* The range name string will automatically be truncated, we don't
	 * need to truncate the "types" list because it will not grow
	 * indefinitely
	 */
	names = range_list_to_string (me->selection);	
	me->parent.cmd_descriptor = g_strdup_printf (_("Clearing %s in %s"), types->str, names->str);
	
	g_string_free (names, TRUE);
	g_string_free (types, TRUE);
	
	/* Register the command object */
	return command_push_undo (wbc, obj);
}

/******************************************************************/

#define CMD_FORMAT_TYPE        (cmd_format_get_type ())
#define CMD_FORMAT(o)          (GTK_CHECK_CAST ((o), CMD_FORMAT_TYPE, CmdFormat))

typedef struct {
	CellPos pos;
	StyleList *styles;
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
			SpanCalcFlags flags = sheet_style_set_list (me->sheet,
					    &os->pos, FALSE, os->styles);

			g_return_val_if_fail (l2 && l2->data, TRUE);

			r = l2->data;
			sheet_range_calc_spans (me->sheet, *r, flags);
			if (flags != SPANCALC_SIMPLE)
				rows_height_update (me->sheet, r, TRUE);
			sheet_flag_format_update_range (me->sheet, r);
		}
	}

	return FALSE;
}

static gboolean
cmd_format_redo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	CmdFormat *me = CMD_FORMAT (cmd);
	GSList    *l;

	g_return_val_if_fail (me != NULL, TRUE);

	for (l = me->selection; l; l = l->next) {
		if (me->borders) {
			sheet_style_apply_border (me->sheet, l->data,
						  me->borders);
			if (me->new_style == NULL)
				sheet_redraw_range (me->sheet, l->data);
		}
		if (me->new_style) {
			mstyle_ref (me->new_style);
			sheet_apply_style (me->sheet, l->data, me->new_style);
		}
		sheet_flag_format_update_range (me->sheet, l->data);
	}

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
				style_list_free (os->styles);

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
 * @opt_translated_name : An optional name to use in place of 'Format Cells'
 *
 * If borders is non NULL, then the StyleBorder references are passed,
 * the MStyle reference is also passed.
 *
 * It absorbs the reference to the style.
 *
 * Return value: TRUE if there was a problem
 **/
gboolean
cmd_format (WorkbookControl *wbc, Sheet *sheet,
	    MStyle *style, StyleBorder **borders,
	    char const *opt_translated_name)
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

	me->parent.sheet = sheet;
	me->parent.size = 1;  /* Updated below.  */

	me->old_styles = NULL;
	for (l = me->selection; l; l = l->next) {
		CmdFormatOldStyle *os;
		Range range = *((Range const *)l->data);

		/* Store the containing range to handle borders */
		if (borders != NULL) {
			if (range.start.col > 0) range.start.col--;
			if (range.start.row > 0) range.start.row--;
			if (range.end.col < SHEET_MAX_COLS-1) range.end.col++;
			if (range.end.row < SHEET_MAX_ROWS-1) range.end.row++;
		}

		os = g_new (CmdFormatOldStyle, 1);

		os->styles = sheet_style_get_list (sheet, &range);
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

	if (opt_translated_name == NULL) {
		GString *names = range_list_to_string (me->selection);
		
		me->parent.cmd_descriptor = g_strdup_printf ("Changing format of %s", names->str);
		g_string_free (names, TRUE);
	} else
		me->parent.cmd_descriptor = g_strdup (opt_translated_name);

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

	me->parent.sheet = NULL;
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
					     _("Set Date/Time"), NULL);
		return TRUE;
	}

	obj = gtk_type_new (CMD_SET_DATE_TIME_TYPE);
	me = CMD_SET_DATE_TIME (obj);

	/* Store the specs for the object */
	me->pos.sheet = sheet;
	me->pos.eval = *pos;
	me->is_date = is_date;
	me->contents = NULL;

	me->parent.sheet = sheet;
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
	GString *list;
	gboolean is_single;

	g_return_val_if_fail (sheet != NULL, TRUE);

	obj = gtk_type_new (CMD_RESIZE_COLROW_TYPE);
	me = CMD_RESIZE_COLROW (obj);

	/* Store the specs for the object */
	me->sheet = sheet;
	me->is_cols = is_cols;
	me->selection = selection;
	me->saved_sizes = NULL;
	me->new_size = new_size;

	me->parent.sheet = sheet;
	me->parent.size = 1;  /* Changed in initial redo.  */

	list = colrow_index_list_to_string (selection, is_cols, &is_single);
	/* Make sure the string doesn't get overly wide */
	if (strlen (list->str) > MAX_DESCRIPTOR_WIDTH) {
		g_string_truncate (list, MAX_DESCRIPTOR_WIDTH - 3);
		g_string_append (list, "...");
	}

	if (is_single) {
		if (new_size < 0)
			me->parent.cmd_descriptor = is_cols
				? g_strdup_printf (_("Autofitting column %s"), list->str)
				: g_strdup_printf (_("Autofitting row %s"), list->str);
		else
			me->parent.cmd_descriptor = is_cols
				? g_strdup_printf (_("Setting width of column %s to %d pixels"),
						   list->str, new_size)
				: g_strdup_printf (_("Setting height of row %s to %d pixels"),
						   list->str, new_size);
	} else {
		if (new_size < 0)
			me->parent.cmd_descriptor = is_cols
				? g_strdup_printf (_("Autofitting columns %s"), list->str)
				: g_strdup_printf (_("Autofitting columns %s"), list->str);
		else
			me->parent.cmd_descriptor = is_cols
				? g_strdup_printf (_("Setting width of columns %s to %d pixels"),
						   list->str, new_size)
				: g_strdup_printf (_("Setting height of rows %s to %d pixels"),
						   list->str, new_size);
	}

	g_string_free (list, TRUE);
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

	if (me->data != NULL) {
		sort_data_destroy (me->data);
		me->data = NULL;
	}
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
	} else
		sort_position (wbc, me->data, me->perm);

	return FALSE;
}

gboolean
cmd_sort (WorkbookControl *wbc, SortData *data)
{
	GtkObject *obj;
	CmdSort *me;
	char *desc;

	g_return_val_if_fail (data != NULL, TRUE);

	desc = g_strdup_printf (_("Sorting %s"), range_name (data->range));
	if (sheet_range_contains_region (data->sheet, data->range, wbc, desc)) {
		sort_data_destroy (data);
		g_free (desc);
		return TRUE;
	}

	obj = gtk_type_new (CMD_SORT_TYPE);
	me = CMD_SORT (obj);

	me->data = data;
	me->perm = NULL;
	me->inv = NULL;

	me->parent.sheet = data->sheet;
	me->parent.size = 1;  /* Changed in initial redo.  */
	me->parent.cmd_descriptor = desc;

	/* Register the command object */
	return command_push_undo (wbc, obj);
}

/******************************************************************/

#define CMD_COLROW_HIDE_TYPE        (cmd_colrow_hide_get_type ())
#define CMD_COLROW_HIDE(o)          (GTK_CHECK_CAST ((o), CMD_COLROW_HIDE_TYPE, CmdColRowHide))

typedef struct
{
	GnumericCommand parent;

	Sheet         *sheet;
	gboolean       is_cols;
	gboolean       visible;
	ColRowVisList *elements;
} CmdColRowHide;

GNUMERIC_MAKE_COMMAND (CmdColRowHide, cmd_colrow_hide);

static void
cmd_colrow_hide_correct_selection (CmdColRowHide *me)
{
	int x, y, index;
	
	/*
	 * Make sure the selection/cursor is set to a visible row/col
	 */
	index = colrow_find_adjacent_visible (me->sheet, me->is_cols,
					      me->is_cols
					      ? me->sheet->edit_pos.col
					      : me->sheet->edit_pos.row,
					      TRUE);

	x = me->is_cols ? me->sheet->edit_pos.row : index;
	y = me->is_cols ? index : me->sheet->edit_pos.col;
					
	sheet_selection_reset (me->sheet);

	if (index != -1) {
		if (me->is_cols)
			sheet_selection_add_range (me->sheet, y, x, y, 0,
						   y, SHEET_MAX_ROWS - 1);
		else
			sheet_selection_add_range (me->sheet, y, x, 0, x,
						   SHEET_MAX_COLS - 1, x);
	}
}

static gboolean
cmd_colrow_hide_undo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	CmdColRowHide *me = CMD_COLROW_HIDE (cmd);

	g_return_val_if_fail (me != NULL, TRUE);

	colrow_set_visibility_list (me->sheet, me->is_cols,
				    !me->visible, me->elements);

	if (me->visible == TRUE)
		cmd_colrow_hide_correct_selection (me);

	return FALSE;
}

static gboolean
cmd_colrow_hide_redo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	CmdColRowHide *me = CMD_COLROW_HIDE (cmd);

	g_return_val_if_fail (me != NULL, TRUE);

	colrow_set_visibility_list (me->sheet, me->is_cols,
				    me->visible, me->elements);

	if (me->visible != TRUE)
		cmd_colrow_hide_correct_selection (me);

	return FALSE;
}

static void
cmd_colrow_hide_destroy (GtkObject *cmd)
{
	CmdColRowHide *me = CMD_COLROW_HIDE (cmd);
	me->elements = colrow_vis_list_destroy (me->elements);
	gnumeric_command_destroy (cmd);
}

gboolean
cmd_colrow_hide_selection (WorkbookControl *wbc, Sheet *sheet,
			   gboolean is_cols, gboolean visible)
{
	GtkObject *obj;
	CmdColRowHide *me;

	g_return_val_if_fail (sheet != NULL, TRUE);

	obj = gtk_type_new (CMD_COLROW_HIDE_TYPE);
	me = CMD_COLROW_HIDE (obj);

	me->sheet = sheet;
	me->is_cols = is_cols;
	me->visible = visible;
	me->elements = colrow_get_visiblity_toggle (sheet, is_cols, visible);

	me->parent.sheet = sheet;
	me->parent.size = 1 + g_slist_length (me->elements);
	me->parent.cmd_descriptor = g_strdup (is_cols
		? (visible ? _("Unhide columns") : _("Hide columns"))
		: (visible ? _("Unhide rows") : _("Hide rows")));

	/* Register the command object */
	return command_push_undo (wbc, obj);
}

gboolean
cmd_colrow_outline_change (WorkbookControl *wbc, Sheet *sheet,
			   gboolean is_cols, int index, int depth)
{
	GtkObject *obj;
	CmdColRowHide *me;
	ColRowInfo const *cri;
	int first = -1, last = -1;
	gboolean visible = FALSE;
	int d;

	g_return_val_if_fail (IS_SHEET (sheet), TRUE);

	cri = is_cols ? sheet_col_get (sheet, index)
		      : sheet_row_get (sheet, index);

	d = (cri != NULL) ?  cri->outline_level : 0;
	if (depth > d)
		depth = d;

	/* Nodes only collapse when selected directly, selecting at a lower
	 * level is a standard toggle.
	 */
	if (depth == d) {
		if ((is_cols ? sheet->outline_symbols_right : sheet->outline_symbols_below)) {
			if (index > 0) {
				ColRowInfo const *prev = is_cols ? sheet_col_get (sheet, index-1)
					: sheet_row_get (sheet, index-1);

				if (prev != NULL && prev->outline_level > d) {
					visible = ((d+1) == prev->outline_level &&
						   cri != NULL && cri->is_collapsed);
					last = index - 1;
					first = colrow_find_outline_bound (sheet, is_cols,
									   index-1, prev->outline_level, FALSE);
				}
			}
		} else if (index+1 < colrow_max (is_cols)) {
			ColRowInfo const *next = is_cols ? sheet_col_get (sheet, index+1)
				: sheet_row_get (sheet, index-1);

			if (next != NULL && next->outline_level > d) {
				visible = ((d+1) == next->outline_level &&
					   cri != NULL && cri->is_collapsed);
				first = index + 1;
				last = colrow_find_outline_bound (sheet, is_cols,
					index+1, next->outline_level, TRUE);
			}
		}
	}

	/* If nothing done yet do a simple collapse */
	if (first < 0 && cri != NULL && cri->outline_level > 0) {
		if (depth < d)
			++depth;
		first = colrow_find_outline_bound (sheet, is_cols, index, depth, FALSE);
		last = colrow_find_outline_bound (sheet, is_cols, index, depth, TRUE);
		visible = FALSE;

		if (first == last && depth > cri->outline_level)
			return TRUE;
	}

	if (first < 0 || last < 0)
		return TRUE;

	obj = gtk_type_new (CMD_COLROW_HIDE_TYPE);
	me = CMD_COLROW_HIDE (obj);

	me->sheet = sheet;
	me->is_cols = is_cols;
	me->visible = visible;
	me->elements = colrow_get_outline_toggle (sheet, is_cols, visible,
						  first, last);

	me->parent.sheet = sheet;
	me->parent.size = 1 + g_slist_length (me->elements);
	me->parent.cmd_descriptor = g_strdup (is_cols
		? (visible ? _("Expand columns") : _("Collapse columns"))
		: (visible ? _("Expand rows") : _("Collapse rows")));

	/* Register the command object */
	return command_push_undo (wbc, obj);
}

/******************************************************************/

#define CMD_PASTE_CUT_TYPE        (cmd_paste_cut_get_type ())
#define CMD_PASTE_CUT(o)          (GTK_CHECK_CAST ((o), CMD_PASTE_CUT_TYPE, CmdPasteCut))

typedef struct
{
	GnumericCommand parent;

	ExprRelocateInfo   info;
	GSList		  *paste_content;
	GSList		  *reloc_storage;
	gboolean	   move_selection;
	ColRowRLESizeList *saved_sizes;
} CmdPasteCut;

GNUMERIC_MAKE_COMMAND (CmdPasteCut, cmd_paste_cut);

typedef struct
{
	PasteTarget pt;
	CellRegion *contents;
} PasteContent;

/**
 * cmd_paste_cut_update_origin :
 *
 * Utility routine to update things whne we are transfering between sheets and
 * workbooks.
 */
static void
cmd_paste_cut_update_origin (ExprRelocateInfo const  *info, WorkbookControl *wbc)
{
	/* Dirty and update both sheets */
	if (info->origin_sheet != info->target_sheet) {
		sheet_set_dirty (info->target_sheet, TRUE);

		/* An if necessary both workbooks */
		if (info->origin_sheet->workbook != info->target_sheet->workbook)
			workbook_recalc (info->origin_sheet->workbook);
		sheet_update (info->origin_sheet);
	}
}

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
		cellregion_free (pc->contents);
		g_free (pc);
	}

	/* Restore the original row heights */
	colrow_restore_sizes (me->info.target_sheet, FALSE, me->info.origin.start.row + me->info.row_offset,
			      me->info.origin.end.row + me->info.row_offset, me->saved_sizes);
	me->saved_sizes = NULL;

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

	cmd_paste_cut_update_origin (&me->info, wbc);

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

	range_ensure_sanity (&tmp);

	sheet_move_range (wbc, &me->info, &me->reloc_storage);

	cmd_paste_cut_update_origin (&me->info, wbc);

	/* Backup row heights and adjust row heights to fit */
	me->saved_sizes = colrow_save_sizes (me->info.target_sheet, FALSE, tmp.start.row, tmp.end.row);
	rows_height_update (me->info.target_sheet, &tmp, FALSE);

	/* Make sure the destination is selected */
	if (me->move_selection)
		sheet_selection_set (me->info.target_sheet,
				     tmp.start.col, tmp.start.row,
				     tmp.start.col, tmp.start.row,
				     tmp.end.col, tmp.end.row);

	return FALSE;
}

static void
cmd_paste_cut_destroy (GtkObject *cmd)
{
	CmdPasteCut *me = CMD_PASTE_CUT (cmd);

	if (me->saved_sizes) {
		colrow_rle_size_list_destroy (me->saved_sizes);
		me->saved_sizes = NULL;
	}
	while (me->paste_content) {
		PasteContent *pc = me->paste_content->data;
		me->paste_content = g_slist_remove (me->paste_content, pc);
		cellregion_free (pc->contents);
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
	       gboolean move_selection, char *descriptor)
{
	GtkObject *obj;
	CmdPasteCut *me;
	Range r;

	/* FIXME: Do we want to show the destination range as well ? */
	if (descriptor == NULL)
		descriptor = g_strdup_printf (_("Moving %s"),
					      range_name (&info->origin));

	g_return_val_if_fail (info != NULL, TRUE);

	r = info->origin;
	if (range_translate (&r, info->col_offset, info->row_offset)) {

		gnumeric_error_invalid (COMMAND_CONTEXT (wbc), descriptor,
					_("is beyond sheet boundaries"));
		g_free (descriptor);
		return TRUE;
	}

	/* Check array subdivision & merged regions */
	if (sheet_range_splits_region (info->target_sheet, &r,
		(info->origin_sheet == info->target_sheet)
		? &info->origin : NULL, wbc, descriptor)) {
		g_free (descriptor);
		return TRUE;
	}

	obj = gtk_type_new (CMD_PASTE_CUT_TYPE);
	me = CMD_PASTE_CUT (obj);

	/* Store the specs for the object */
	me->info = *info;
	me->paste_content  = NULL;
	me->reloc_storage  = NULL;
	me->move_selection = move_selection;
	me->saved_sizes    = NULL;

	me->parent.sheet = info->target_sheet;
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

	CellRegion        *content;
	PasteTarget        dst;
	gboolean           has_been_through_cycle;
	ColRowRLESizeList *saved_sizes;
} CmdPasteCopy;

GNUMERIC_MAKE_COMMAND (CmdPasteCopy, cmd_paste_copy);

static gboolean
cmd_paste_copy_impl (GnumericCommand *cmd, WorkbookControl *wbc,
		     gboolean is_undo)
{
	CmdPasteCopy *me = CMD_PASTE_COPY (cmd);
	CellRegion *content;

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->content != NULL, TRUE);

	content = clipboard_copy_range (me->dst.sheet, &me->dst.range);
	if (clipboard_paste_region (wbc, &me->dst, me->content)) {
		/* There was a problem, avoid leaking */
		cellregion_free (content);
		return TRUE;
	}

	if (me->has_been_through_cycle)
		cellregion_free (me->content);
	else {
		/* Save the content */
		me->dst.paste_flags = PASTE_CONTENT |
			(me->dst.paste_flags & PASTE_FORMATS);
	}
	
	if (is_undo) {
		colrow_restore_sizes (me->dst.sheet, FALSE, me->dst.range.start.row,
				      me->dst.range.end.row, me->saved_sizes);
		me->saved_sizes = NULL;
	} else {
		me->saved_sizes = colrow_save_sizes (me->dst.sheet, FALSE, me->dst.range.start.row,
						     me->dst.range.end.row);
		rows_height_update (me->dst.sheet, &me->dst.range, FALSE);
	}

	me->content = content;
	me->has_been_through_cycle = TRUE;

	/* Make the newly pasted content the selection (this queues a redraw) */
	sheet_selection_reset (me->dst.sheet);
	sheet_selection_add_range (me->dst.sheet,
				   me->dst.range.start.col, me->dst.range.start.row,
				   me->dst.range.start.col, me->dst.range.start.row,
				   me->dst.range.end.col, me->dst.range.end.row);
	sheet_make_cell_visible	(me->dst.sheet,
				 me->dst.range.start.col, me->dst.range.start.row);

	return FALSE;
}

static gboolean
cmd_paste_copy_undo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	return cmd_paste_copy_impl (cmd, wbc, TRUE);
}

static gboolean
cmd_paste_copy_redo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	return cmd_paste_copy_impl (cmd, wbc, FALSE);
}

static void
cmd_paste_copy_destroy (GtkObject *cmd)
{
	CmdPasteCopy *me = CMD_PASTE_COPY (cmd);

	if (me->saved_sizes) {
		colrow_rle_size_list_destroy (me->saved_sizes);
		me->saved_sizes = NULL;
	}
	if (me->content) {
		if (me->has_been_through_cycle)
			cellregion_free (me->content);
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
	me->parent.sheet = pt->sheet;
	me->parent.size = 1;  /* FIXME?  */
	me->parent.cmd_descriptor = g_strdup_printf (_("Pasting into %s"),
						     range_name (&pt->range));
	me->dst = *pt;
	me->content = content;
	me->has_been_through_cycle = FALSE;
	me->saved_sizes = NULL;
	
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

	/* Use translate to do a quiet sanity check */
	if (range_translate (&me->dst.range, 0, 0)) {
		gnumeric_error_invalid (COMMAND_CONTEXT (wbc),
					me->parent.cmd_descriptor,
					_("is beyond sheet boundaries"));
		gtk_object_destroy (GTK_OBJECT (me));
		return TRUE;
	}

	/* Check array subdivision & merged regions */
	if (sheet_range_splits_region (pt->sheet, &me->dst.range,
				       NULL, wbc, me->parent.cmd_descriptor)) {
		gtk_object_destroy (GTK_OBJECT (me));
		return TRUE;
	}

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
	cellregion_free (me->content);
	me->content = NULL;

	if (res)
		return TRUE;

	/* Make the newly pasted content the selection (this queues a redraw) */
	sheet_selection_reset (me->dst.sheet);
	sheet_selection_add_range (me->dst.sheet,
				   me->base_col, me->base_row,
				   me->base_col, me->base_row,
				   me->base_col + me->w-1,
				   me->base_row + me->h-1);
	sheet_make_cell_visible	(me->dst.sheet, me->base_col, me->base_row);

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
	sheet_clear_region (wbc, me->dst.sheet,
			    me->dst.range.start.col, me->dst.range.start.row,
			    me->dst.range.end.col,   me->dst.range.end.row,
			    CLEAR_VALUES | CLEAR_NOCHECKARRAY);

	if (me->parent.size == 1)
		me->parent.size += (g_list_length (me->content->content) +
				    g_list_length (me->content->styles) +
				    1);
	sheet_autofill (me->dst.sheet,
			me->base_col, me->base_row, me->w, me->h,
			me->end_col, me->end_row);

	/* Make the newly filled content the selection (this queues a redraw) */
	sheet_selection_reset (me->dst.sheet);
	sheet_selection_add_range (me->dst.sheet,
				   me->base_col, me->base_row,
				   me->base_col, me->base_row,
				   me->end_col, me->end_row);

	deps = sheet_region_get_deps (me->dst.sheet, &me->dst.range);
	if (deps)
		dependent_queue_recalc_list (deps, TRUE);
	sheet_range_calc_spans (me->dst.sheet, me->dst.range, SPANCALC_RENDER);
	sheet_flag_status_update_range (me->dst.sheet, &me->dst.range);
	sheet_make_cell_visible	(me->dst.sheet, me->base_col, me->base_row);

	return FALSE;
}

static void
cmd_autofill_destroy (GtkObject *cmd)
{
	CmdAutofill *me = CMD_AUTOFILL (cmd);

	if (me->content) {
		cellregion_free (me->content);
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
	Range r;

	g_return_val_if_fail (IS_SHEET (sheet), TRUE);

	/* This would be meaningless */
	if (base_col+w-1 == end_col && base_row+h-1 == end_row)
		return FALSE;

	if (end_col != base_col + w - 1)
		range_init (&r, base_col + w, base_row,
			    end_col, end_row);
	else
		range_init (&r, base_col, base_row + h,
			    end_col, end_row);

	/* Check arrays or merged regions */
	if (sheet_range_splits_region (sheet, &r, NULL, wbc, _("Autofill")))
		return TRUE;

	obj = gtk_type_new (CMD_AUTOFILL_TYPE);
	me = CMD_AUTOFILL (obj);

	/* Store the specs for the object */
	me->content = NULL;
	me->dst.sheet = sheet;
	me->dst.paste_flags = PASTE_CONTENT | PASTE_FORMATS;
	me->dst.range = r;

	me->base_col = base_col;
	me->base_row = base_row,
	me->w = w;
	me->h = h;
	me->end_col = end_col;
	me->end_row = end_row;

	me->parent.sheet = sheet;
	me->parent.size = 1;  /* Changed in initial redo.  */
	me->parent.cmd_descriptor = g_strdup_printf (_("Autofilling %s"),
						     range_name (&me->dst.range));

	/* Register the command object */
	return command_push_undo (wbc, obj);
}

/******************************************************************/

#define CMD_AUTOFORMAT_TYPE        (cmd_autoformat_get_type ())
#define CMD_AUTOFORMAT(o)          (GTK_CHECK_CAST ((o), CMD_AUTOFORMAT_TYPE, CmdAutoFormat))

typedef struct {
	CellPos pos;
	StyleList *styles;
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
			SpanCalcFlags flags = sheet_style_set_list (me->sheet,
					    &os->pos, FALSE, os->styles);

			g_return_val_if_fail (l2 && l2->data, TRUE);

			r = l2->data;
			sheet_range_calc_spans (me->sheet, *r, flags);
			if (flags != SPANCALC_SIMPLE)
				rows_height_update (me->sheet, r, TRUE);
		}
	}

	return FALSE;
}

static gboolean
cmd_autoformat_redo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	CmdAutoFormat *me = CMD_AUTOFORMAT (cmd);

	g_return_val_if_fail (me != NULL, TRUE);

	format_template_apply_to_sheet_regions (me->ft, me->sheet, me->selections);

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
				style_list_free (os->styles);

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
 * cmd_autoformat:
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
	GString   *names;
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

		os->styles = sheet_style_get_list (sheet, &range);
		os->pos = range.start;

		me->old_styles = g_slist_append (me->old_styles, os);
	}

	me->parent.sheet = sheet;
	me->parent.size = 1;  /* FIXME?  */

	names = range_list_to_string (me->selections);
	me->parent.cmd_descriptor = g_strdup_printf (_("Autoformatting %s"),
						     names->str);
	g_string_free (names, TRUE);

	/* Register the command object */
	return command_push_undo (wbc, obj);
}

/******************************************************************/

#define CMD_UNMERGE_CELLS_TYPE        (cmd_unmerge_cells_get_type ())
#define CMD_UNMERGE_CELLS(o)          (GTK_CHECK_CAST ((o), CMD_UNMERGE_CELLS_TYPE, CmdUnmergeCells))

typedef struct {
	GnumericCommand parent;

	Sheet	*sheet;
	GArray	*unmerged_regions;
	GArray	*ranges;
} CmdUnmergeCells;

GNUMERIC_MAKE_COMMAND (CmdUnmergeCells, cmd_unmerge_cells);

static gboolean
cmd_unmerge_cells_undo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	CmdUnmergeCells *me = CMD_UNMERGE_CELLS (cmd);
	unsigned i;

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->unmerged_regions != NULL, TRUE);

	for (i = 0 ; i < me->unmerged_regions->len ; ++i) {
		Range const *tmp = &(g_array_index (me->unmerged_regions, Range, i));
		sheet_merge_add (wbc, me->parent.sheet, tmp, FALSE);
		sheet_range_calc_spans (me->parent.sheet, *tmp, SPANCALC_RE_RENDER);
	}

	g_array_free (me->unmerged_regions, TRUE);
	me->unmerged_regions = NULL;

	return FALSE;
}

static gboolean
cmd_unmerge_cells_redo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	CmdUnmergeCells *me = CMD_UNMERGE_CELLS (cmd);
	unsigned i;

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->unmerged_regions == NULL, TRUE);

	me->unmerged_regions = g_array_new (FALSE, FALSE, sizeof (Range));
	for (i = 0 ; i < me->ranges->len ; ++i) {
		GSList *ptr, *merged = sheet_merge_get_overlap (me->parent.sheet,
			&(g_array_index (me->ranges, Range, i)));
		for (ptr = merged ; ptr != NULL ; ptr = ptr->next) {
			Range tmp = *(Range *)(ptr->data);
			g_array_append_val (me->unmerged_regions, tmp);
			sheet_merge_remove (wbc, me->parent.sheet, &tmp);
			sheet_range_calc_spans (me->parent.sheet, tmp,
							SPANCALC_RE_RENDER);
		}
		g_slist_free (merged);
	}

	return FALSE;
}

static void
cmd_unmerge_cells_destroy (GtkObject *cmd)
{
	CmdUnmergeCells *me = CMD_UNMERGE_CELLS (cmd);

	if (me->unmerged_regions != NULL) {
		g_array_free (me->unmerged_regions, TRUE);
		me->unmerged_regions = NULL;
	}
	if (me->ranges != NULL) {
		g_array_free (me->ranges, TRUE);
		me->ranges = NULL;
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
cmd_unmerge_cells (WorkbookControl *wbc, Sheet *sheet, GSList const *selection)
{
	GtkObject *obj;
	CmdUnmergeCells *me;
	GString *names;

	g_return_val_if_fail (sheet != NULL, TRUE);

	obj = gtk_type_new (CMD_UNMERGE_CELLS_TYPE);
	me = CMD_UNMERGE_CELLS (obj);

	me->parent.sheet = sheet;
	me->parent.size = 1;

	names = range_list_to_string (selection);
	me->parent.cmd_descriptor = g_strdup_printf (_("Unmerging %s"), names->str);
	g_string_free (names, TRUE);
	
	me->unmerged_regions = NULL;
	me->ranges = g_array_new (FALSE, FALSE, sizeof (Range));
	for ( ; selection != NULL ; selection = selection->next) {
		GSList *merged = sheet_merge_get_overlap (sheet, selection->data);
		if (merged != NULL) {
			g_array_append_val (me->ranges, *(Range *)selection->data);
			g_slist_free (merged);
		}
	}

	if (me->ranges->len <= 0) {
		gtk_object_destroy (GTK_OBJECT (me));
		return TRUE;
	}

	/* Register the command object */
	return command_push_undo (wbc, obj);
}

/******************************************************************/

#define CMD_MERGE_CELLS_TYPE        (cmd_merge_cells_get_type ())
#define CMD_MERGE_CELLS(o)          (GTK_CHECK_CAST ((o), CMD_MERGE_CELLS_TYPE, CmdMergeCells))

typedef struct {
	GnumericCommand parent;
	GArray	*ranges;
	GSList	*old_content;
} CmdMergeCells;

GNUMERIC_MAKE_COMMAND (CmdMergeCells, cmd_merge_cells)

static gboolean
cmd_merge_cells_undo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	CmdMergeCells *me = CMD_MERGE_CELLS (cmd);
	unsigned i;

	g_return_val_if_fail (me != NULL, TRUE);

	for (i = 0 ; i < me->ranges->len ; ++i) {
		Range const * r = &(g_array_index (me->ranges, Range, i));
		sheet_merge_remove (wbc, me->parent.sheet, r);
	}

	for (i = 0 ; i < me->ranges->len ; ++i) {
		Range const * r = &(g_array_index (me->ranges, Range, i));
		PasteTarget pt;
		CellRegion * c;

		g_return_val_if_fail (me->old_content != NULL, TRUE);

		c = me->old_content->data;
		clipboard_paste_region (wbc,
					paste_target_init (&pt, me->parent.sheet, r,
							   PASTE_CONTENT | PASTE_FORMATS | PASTE_IGNORE_COMMENTS),
					c);
		cellregion_free (c);
		me->old_content = g_slist_remove (me->old_content, c);
	}
	g_return_val_if_fail (me->old_content == NULL, TRUE);

	return FALSE;
}

static gboolean
cmd_merge_cells_redo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	CmdMergeCells *me = CMD_MERGE_CELLS (cmd);
	Sheet *sheet;
	unsigned i;

	g_return_val_if_fail (me != NULL, TRUE);

	sheet = me->parent.sheet;
	for (i = 0 ; i < me->ranges->len ; ++i) {
		Range const *r = &(g_array_index (me->ranges, Range, i));
		GSList *ptr, *merged = sheet_merge_get_overlap (sheet, r);

		/* save content before removing contained merged regions */
		me->old_content = g_slist_prepend (me->old_content,
			clipboard_copy_range (sheet, r));
		for (ptr = merged ; ptr != NULL ; ptr = ptr->next)
			sheet_merge_remove (wbc, sheet, ptr->data);
		g_slist_free (merged);

		sheet_merge_add (wbc, sheet, r, TRUE);
	}

	me->old_content = g_slist_reverse (me->old_content);
	return FALSE;
}

static void
cmd_merge_cells_destroy (GtkObject *cmd)
{
	CmdMergeCells *me = CMD_MERGE_CELLS (cmd);

	if (me->old_content != NULL) {
		GSList *l;
		for (l = me->old_content ; l != NULL ; l = g_slist_remove (l, l->data))
			cellregion_free (l->data);
		me->old_content = NULL;
	}

	if (me->ranges != NULL) {
		g_array_free (me->ranges, TRUE);
		me->ranges = NULL;
	}

	gnumeric_command_destroy (cmd);
}

/**
 * cmd_merge_cells:
 * @context: the context.
 *
 * Return value: TRUE if there was a problem
 **/
gboolean
cmd_merge_cells (WorkbookControl *wbc, Sheet *sheet, GSList const *selection)
{
	GtkObject *obj;
	CmdMergeCells *me;
	GString *names;

	g_return_val_if_fail (IS_SHEET (sheet), TRUE);

	obj = gtk_type_new (CMD_MERGE_CELLS_TYPE);
	me = CMD_MERGE_CELLS (obj);

	me->parent.sheet = sheet;
	me->parent.size = 1;

	names = range_list_to_string (selection);
	me->parent.cmd_descriptor = g_strdup_printf (_("Merging %s"),
						     names->str);
	g_string_free (names, TRUE);

	me->ranges = g_array_new (FALSE, FALSE, sizeof (Range));
	for ( ; selection != NULL ; selection = selection->next) {
		Range const *exist;
		Range const *r = selection->data;
		if (range_is_singleton (selection->data))
			continue;
		if (NULL != (exist = sheet_merge_is_corner (sheet, &r->start)) &&
		    range_equal (r, exist))
			continue;
		g_array_append_val (me->ranges, *(Range *)selection->data);
	}

	if (me->ranges->len <= 0) {
		gtk_object_destroy (GTK_OBJECT (me));
		return TRUE;
	}

	/* Register the command object */
	return command_push_undo (wbc, obj);
}

/******************************************************************/

#define CMD_SEARCH_REPLACE_TYPE		(cmd_search_replace_get_type())
#define CMD_SEARCH_REPLACE(o)		(GTK_CHECK_CAST ((o), CMD_SEARCH_REPLACE_TYPE, CmdSearchReplace))

typedef struct
{
	GnumericCommand parent;
	SearchReplace *sr;

	/*
	 * Undo/redo use this list of SearchReplaceItems to do their
	 * work.  Note, that it is possible for a cell to occur
	 * multiple times in the list.
	 */
	GList *cells;
} CmdSearchReplace;

GNUMERIC_MAKE_COMMAND (CmdSearchReplace, cmd_search_replace);

typedef enum { SRI_text, SRI_comment } SearchReplaceItemType;

typedef struct {
	EvalPos pos;
	SearchReplaceItemType old_type, new_type;
	union {
		char *text;
		char *comment;
	} old, new;
} SearchReplaceItem;


static void
cmd_search_replace_update_after_action (CmdSearchReplace *me)
{
	GList *tmp;
	Sheet *last_sheet = NULL;

	for (tmp = me->cells; tmp; tmp = tmp->next) {
		SearchReplaceItem *sri = tmp->data;
		if (sri->pos.sheet != last_sheet) {
			last_sheet = sri->pos.sheet;
			update_after_action (last_sheet);
		}
	}
}


static gboolean
cmd_search_replace_undo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	CmdSearchReplace *me = CMD_SEARCH_REPLACE (cmd);
	GList *tmp;

	/* Undo does replacements backwards.  */
	for (tmp = g_list_last (me->cells); tmp; tmp = tmp->prev) {
		SearchReplaceItem *sri = tmp->data;
		switch (sri->old_type) {
		case SRI_text:
		{
			Cell *cell = sheet_cell_get (sri->pos.sheet,
						     sri->pos.eval.col,
						     sri->pos.eval.row);
			sheet_cell_set_text (cell, sri->old.text);
			break;
		}
		case SRI_comment:
		{
			CellComment *comment =
				cell_has_comment_pos (sri->pos.sheet,
						      &sri->pos.eval);
			if (comment) {
				cell_comment_text_set (comment, sri->old.comment);
			} else {
				g_warning ("Undo/redo broken.");
			}
		}
		break;
		}
	}
	cmd_search_replace_update_after_action (me);

	return FALSE;
}

static gboolean
cmd_search_replace_redo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	CmdSearchReplace *me = CMD_SEARCH_REPLACE (cmd);
	GList *tmp;

	/* Redo does replacements forward.  */
	for (tmp = me->cells; tmp; tmp = tmp->next) {
		SearchReplaceItem *sri = tmp->data;
		switch (sri->new_type) {
		case SRI_text:
		{
			Cell *cell = sheet_cell_get (sri->pos.sheet,
						     sri->pos.eval.col,
						     sri->pos.eval.row);
			sheet_cell_set_text (cell, sri->new.text);
			break;
		}
		case SRI_comment:
		{
			CellComment *comment =
				cell_has_comment_pos (sri->pos.sheet,
						      &sri->pos.eval);
			if (comment) {
				cell_comment_text_set (comment, sri->new.comment);
			} else {
				g_warning ("Undo/redo broken.");
			}
		}
		break;
		}
	}
	cmd_search_replace_update_after_action (me);

	return FALSE;
}

static gboolean
cmd_search_replace_do_cell (CmdSearchReplace *me, EvalPos *ep,
			    gboolean test_run)
{
	SearchReplace *sr = me->sr;
	gboolean is_expr, is_value, is_string, is_other;
	Cell *cell = sheet_cell_get (ep->sheet, ep->eval.col, ep->eval.row);
	Value *v = cell ? cell->value : NULL;

	is_expr = cell && cell_has_expr (cell);
	is_value = cell && !is_expr && !cell_is_blank (cell) && v;
	is_string = is_value && (v->type == VALUE_STRING);
	is_other = is_value && !is_string;

	if ((is_expr && sr->replace_expressions) ||
	    (is_string && sr->replace_strings) ||
	    (is_other && sr->replace_other_values)) {
		char *old_text = cell_get_entered_text (cell);
		gboolean initial_quote = (is_value && old_text[0] == '\'');
		char *new_text = search_replace_string (sr, old_text + (initial_quote ? 1 : 0));

		if (new_text) {
			MStyle *mstyle;
			StyleFormat *sf, *cf;
			ExprTree *expr;
			Value *val;
			gboolean err;

			if (initial_quote) {
				/*
				 * The initial quote was not part of the s-a-r,
				 * so tack it back on.
				 */
				char *tmp = g_new (char, strlen (new_text) + 2);
				tmp[0] = '\'';
				strcpy (tmp + 1, new_text);
				g_free (new_text);
				new_text = tmp;
			}

			mstyle = cell_get_mstyle (cell);
			cf = mstyle_get_format (mstyle);
			sf = parse_text_value_or_expr (ep, new_text,
						       &val, &expr, cf);
			/*
			 * FIXME: this is a hack, but parse_text_value_or_expr
			 * does not have a better way of signaling an error.
			 */
			err = val && gnumeric_char_start_expr_p (new_text);

			if (val) value_release (val);
			if (expr) expr_tree_unref (expr);
			if (sf) style_format_unref (sf);

			if (err) {
				if (test_run) {
					if (sr->query_func)
						sr->query_func (SRQ_fail,
								sr,
								cell,
								old_text,
								new_text);
					g_free (old_text);
					g_free (new_text);
					return TRUE;
				} else {
					switch (sr->error_behaviour) {
					case SRE_error: {
						char *tmp = gnumeric_strescape (new_text);
						g_free (new_text);
						new_text = g_strconcat ("=ERROR(", tmp, ")", NULL);
						g_free (tmp);
						err = FALSE;
						break;
					}
					case SRE_string: {
						/* FIXME: quoting isn't right.  */
						char *tmp = gnumeric_strescape (new_text);
						g_free (new_text);
						new_text = tmp;
						err = FALSE;
						break;
					}
					case SRE_fail:
						g_assert_not_reached ();
					case SRE_skip:
					default:
						; /* Nothing */
					}
				}
			}

			if (!err && !test_run) {
				SearchReplaceItem *sri = g_new (SearchReplaceItem, 1);

				sheet_cell_set_text (cell, new_text);

				sri->pos = *ep;
				sri->old_type = sri->new_type = SRI_text;
				sri->old.text = old_text;
				sri->new.text = new_text;
				me->cells = g_list_prepend (me->cells, sri);

				old_text = new_text = NULL;
			}

			g_free (new_text);
		}

		g_free (old_text);
	}

	if (!test_run && sr->replace_comments) {
		CellComment *comment = cell_has_comment_pos (ep->sheet, &ep->eval);
		if (comment) {
			const char *old_text = cell_comment_text_get (comment);
			char *new_text = search_replace_string (sr, old_text);
			if (new_text) {
				gboolean doit = TRUE;
				if (sr->query) {
					/* FIXME. */
				}

				if (doit) {
					SearchReplaceItem *sri = g_new (SearchReplaceItem, 1);
					sri->pos = *ep;
					sri->old_type = sri->new_type = SRI_comment;
					sri->old.comment = g_strdup (old_text);
					sri->new.comment = new_text;
					me->cells = g_list_prepend (me->cells, sri);

					cell_comment_text_set (comment, new_text);
				} else
					g_free (new_text);
			}
		}
	}

	return FALSE;
}

static int
cb_order_sheet_row_col (const void *_a, const void *_b)
{
	const EvalPos *a = *(const EvalPos **)_a;
	const EvalPos *b = *(const EvalPos **)_b;
	int i;

	/* By sheet name.  FIXME: Any better way than this?  */
	i = strcmp (a->sheet->name_unquoted, b->sheet->name_unquoted);

	/* By row number.  */
	if (!i) i = (a->eval.row - b->eval.row);

	/* By column number.  */
	if (!i) i = (a->eval.col - b->eval.col);

	return i;
}


static gboolean
cmd_search_replace_do (CmdSearchReplace *me, Workbook *wb,
		       Sheet *sheet, gboolean test_run)
{
	SearchReplace *sr = me->sr;
	GPtrArray *cells = NULL;
	gboolean result = FALSE;
	unsigned i;

	if (test_run) {
		switch (sr->error_behaviour) {
		case SRE_skip:
		case SRE_query:
		case SRE_error:
		case SRE_string:
			/* An error is not a problem.  */
			return FALSE;

		case SRE_fail:
			; /* Nothing.  */
		}
	}

	/* Collect a list of all cells subject to search.  */
	switch (sr->scope) {
	case SRS_workbook:
		cells = workbook_cells (wb, TRUE);
		break;

	case SRS_sheet:
		cells = sheet_cells (sheet,
				     0, 0, SHEET_MAX_COLS, SHEET_MAX_ROWS,
				     TRUE);
		break;

	case SRS_range:
	{
		int start_col, start_row, end_col, end_row;

		/* FIXME: what about sheet name?  */
		parse_range (sr->range_text,
			     &start_col, &start_row,
			     &end_col, &end_row);

		cells = sheet_cells (sheet,
				     start_col, start_row, end_col, end_row,
				     TRUE);
		break;
	}

	default:
		g_assert_not_reached ();
	}

	/* Sort our cells.  */
	qsort (&g_ptr_array_index (cells, 0),
	       cells->len,
	       sizeof (gpointer),
	       cb_order_sheet_row_col);

	for (i = 0; i < cells->len; i++) {
		EvalPos *ep = g_ptr_array_index (cells, i);
		if (!result) {
			result = cmd_search_replace_do_cell (me, ep, test_run);
		}
		g_free (ep);
	}

	g_ptr_array_free (cells, TRUE);

	if (!test_run) {
		/* Cells were added in the wrong order.  Correct.  */
		me->cells = g_list_reverse (me->cells);

		cmd_search_replace_update_after_action (me);
	}

	return result;
}


static void
cmd_search_replace_destroy (GtkObject *cmd)
{
	CmdSearchReplace *me = CMD_SEARCH_REPLACE (cmd);
	GList *tmp;

	for (tmp = me->cells; tmp; tmp = tmp->next) {
		SearchReplaceItem *sri = tmp->data;
		switch (sri->old_type) {
		case SRI_text:
			g_free (sri->old.text);
			break;
		case SRI_comment:
			g_free (sri->old.comment);
			break;
		}
		switch (sri->new_type) {
		case SRI_text:
			g_free (sri->new.text);
			break;
		case SRI_comment:
			g_free (sri->new.comment);
			break;
		}
		g_free (sri);
	}
	g_list_free (me->cells);
	search_replace_free (me->sr);

	gnumeric_command_destroy (cmd);
}

gboolean
cmd_search_replace (WorkbookControl *wbc, Sheet *sheet, SearchReplace *sr)
{
	GtkObject *obj;
	CmdSearchReplace *me;
	Workbook *wb = wb_control_workbook (wbc);

	g_return_val_if_fail (sr != NULL, TRUE);

	obj = gtk_type_new (CMD_SEARCH_REPLACE_TYPE);
	me = CMD_SEARCH_REPLACE (obj);

	me->cells = NULL;
	me->sr = search_replace_copy (sr);

	me->parent.sheet = NULL;
	me->parent.size = 1;  /* Corrected below. */
	me->parent.cmd_descriptor = g_strdup (_("Search and Replace"));

	if (cmd_search_replace_do (me, wb, sheet, TRUE)) {
		/* There was an error and nothing was done.  */
		gtk_object_unref (obj);
		return TRUE;
	}

	cmd_search_replace_do (me, wb, sheet, FALSE);
	me->parent.size += g_list_length (me->cells);

	/* Register the command object */
	command_register_undo (wbc, obj);
	return FALSE;
}

/******************************************************************/

#define CMD_COLROW_STD_SIZE_TYPE        (cmd_colrow_std_size_get_type ())
#define CMD_COLROW_STD_SIZE(o)          (GTK_CHECK_CAST ((o), CMD_COLROW_STD_SIZE_TYPE, CmdColRowStdSize))

typedef struct
{
	GnumericCommand parent;

	Sheet		*sheet;
	gboolean	 is_cols;
	double		 new_default;
	double           old_default;
} CmdColRowStdSize;

GNUMERIC_MAKE_COMMAND (CmdColRowStdSize, cmd_colrow_std_size);

static gboolean
cmd_colrow_std_size_undo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	CmdColRowStdSize *me = CMD_COLROW_STD_SIZE (cmd);

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->old_default != 0, TRUE);

	if (me->is_cols)
		sheet_col_set_default_size_pts (me->sheet, me->old_default);
	else
		sheet_row_set_default_size_pts (me->sheet, me->old_default);

	me->old_default = 0;

	return FALSE;
}

static gboolean
cmd_colrow_std_size_redo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	CmdColRowStdSize *me = CMD_COLROW_STD_SIZE (cmd);

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->old_default == 0, TRUE);

	if (me->is_cols) {
		me->old_default = sheet_col_get_default_size_pts (me->sheet);
		sheet_col_set_default_size_pts (me->sheet, me->new_default);
	} else {
		me->old_default = sheet_row_get_default_size_pts (me->sheet);
		sheet_row_set_default_size_pts (me->sheet, me->new_default);
	}
	
	return FALSE;
}
static void
cmd_colrow_std_size_destroy (GtkObject *cmd)
{
	gnumeric_command_destroy (cmd);
}

gboolean
cmd_colrow_std_size (WorkbookControl *wbc, Sheet *sheet,
		     gboolean is_cols, double new_default)
{
	GtkObject *obj;
	CmdColRowStdSize *me;

	g_return_val_if_fail (sheet != NULL, TRUE);

	obj = gtk_type_new (CMD_COLROW_STD_SIZE_TYPE);
	me = CMD_COLROW_STD_SIZE (obj);

	/* Store the specs for the object */
	me->sheet = sheet;
	me->is_cols = is_cols;
	me->new_default = new_default;
	me->old_default = 0;

	me->parent.sheet = sheet;
	me->parent.size = 1;  /* Changed in initial redo.  */
	me->parent.cmd_descriptor = is_cols
		? g_strdup_printf (_("Setting default width of columns to %.2fpts"), new_default)
		: g_strdup_printf (_("Setting default height of rows to %.2fpts"), new_default);

	/* Register the command object */
	return command_push_undo (wbc, obj);
}

/******************************************************************/

#define CMD_ZOOM_TYPE        (cmd_zoom_get_type ())
#define CMD_ZOOM(o)     (GTK_CHECK_CAST ((o), CMD_ZOOM_TYPE, CmdZoom))

typedef struct
{
	GnumericCommand parent;

	GSList		*sheets;
	double		 new_factor;
	double          *old_factors;
} CmdZoom;

GNUMERIC_MAKE_COMMAND (CmdZoom, cmd_zoom);

static gboolean
cmd_zoom_undo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	CmdZoom *me = CMD_ZOOM (cmd);
	GSList *l;
	int i;

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->sheets != NULL, TRUE);
	g_return_val_if_fail (me->old_factors != NULL, TRUE);
	
	for (i = 0, l = me->sheets; l != NULL; l = l->next, i++) {
		Sheet *sheet = l->data;
		
		sheet_set_zoom_factor (sheet, me->old_factors[i], FALSE, TRUE);
	}
	
	return FALSE;
}

static gboolean
cmd_zoom_redo (GnumericCommand *cmd, WorkbookControl *wbc)
{
	CmdZoom *me = CMD_ZOOM (cmd);
	GSList *l;
	
	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->sheets != NULL, TRUE);

	for (l = me->sheets; l != NULL; l = l->next) {
		Sheet *sheet = l->data;
		
		sheet_set_zoom_factor (sheet, me->new_factor, FALSE, TRUE);
	}
	
	return FALSE;
}

static void
cmd_zoom_destroy (GtkObject *cmd)
{
	CmdZoom *me = CMD_ZOOM (cmd);
	
	if (me->sheets)
		g_slist_free (me->sheets);
	if (me->old_factors)
		g_free (me->old_factors);
		
	gnumeric_command_destroy (cmd);
}

gboolean
cmd_zoom (WorkbookControl *wbc, GSList *sheets, double factor)
{
	GtkObject *obj;
	CmdZoom *me;
	GString *namelist;
	GSList *l;
	int i;
	
	g_return_val_if_fail (wbc != NULL, TRUE);
	g_return_val_if_fail (sheets != NULL, TRUE);

	obj = gtk_type_new (CMD_ZOOM_TYPE);
	me = CMD_ZOOM (obj);

	/* Store the specs for the object */
	me->sheets = sheets;
	me->old_factors = g_new0 (double, g_slist_length (sheets));
	me->new_factor  = factor;

	/* Make a list of all sheets to zoom and save zoom factor for each */
	namelist = g_string_new ("");
	for (i = 0, l = me->sheets; l != NULL; l = l->next, i++) {
		Sheet *sheet = l->data;

		g_string_append (namelist, sheet->name_unquoted);
		me->old_factors[i] = sheet->last_zoom_factor_used;
		
		if (l->next)
			g_string_append (namelist, ", ");
	}

	/* Make sure the string doesn't get overly wide */
	if (strlen (namelist->str) > MAX_DESCRIPTOR_WIDTH) {
		g_string_truncate (namelist, MAX_DESCRIPTOR_WIDTH - 3);
		g_string_append (namelist, "...");
	}
	
	me->parent.sheet = NULL;
	me->parent.size = 1;
	me->parent.cmd_descriptor =
		g_strdup_printf (_("Zoom %s to %.0f%%"), namelist->str, factor * 100);

	g_string_free (namelist, TRUE);
	
	/* Register the command object */
	return command_push_undo (wbc, obj);
}

/******************************************************************/

/* TODO :
 * - SheetObject creation & manipulation.
 */

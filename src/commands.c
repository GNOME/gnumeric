/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * commands.c: Handlers to undo & redo commands
 *
 * Copyright (C) 1999-2002 Jody Goldberg (jody@gnome.org)
 *
 * Contributors : Almer S. Tigelaar (almer@gnome.org)
 *                Andreas J. Guelzow (aguelzow@taliesin.ca)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as published
 * by the Free Software Foundation.
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
#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include "gnumeric.h"
#include "commands.h"

#include "application.h"
#include "sheet.h"
#include "sheet-view.h"
#include "sheet-style.h"
#include "format.h"
#include "format-template.h"
#include "command-context.h"
#include "workbook-control.h"
#include "workbook-view.h"
#include "workbook-priv.h" /* For the undo/redo queues and the FOREACH */
#include "ranges.h"
#include "sort.h"
#include "dependent.h"
#include "value.h"
#include "expr.h"
#include "expr-name.h"
#include "cell.h"
#include "sheet-merge.h"
#include "parse-util.h"
#include "print-info.h"
#include "clipboard.h"
#include "selection.h"
#include "datetime.h"
#include "colrow.h"
#include "style-border.h"
#include "auto-correct.h"
#include "sheet-autofill.h"
#include "mstyle.h"
#include "search.h"
#include "gutils.h"
#include "gui-util.h"
#include "sheet-object-cell-comment.h"
#include "sheet-object-widget.h"
#include "sheet-object.h"
#include "sheet-object-graph.h"
#include "sheet-control.h"
#include "style-color.h"
#include "summary.h"
#include "auto-format.h"
#include "tools/dao.h"
#include "gnumeric-gconf.h"
#include "scenarios.h"
#include "data-shuffling.h"
#include "tools/tabulate.h"

#include <gsf/gsf-impl-utils.h>
#include <string.h>
#include <goffice/graph/gog-graph.h>

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
 *    duplicating work.  The lower levels can queue redraws if they must, and
 *    flag state changes but the call to workbook_recalc and sheet_update is
 *    by GnmCommand.
 *
 * FIXME: Filter the list of commands when a sheet is deleted.
 *
 * TODO : Possibly clear lists on save.
 *
 * TODO : Reqs for selective undo
 *
 * Future thoughts
 * - undoable preference setting ?  XL does not have this.  Do we want it ?
 */
/******************************************************************/

#define GNM_COMMAND_TYPE        (gnm_command_get_type ())
#define GNM_COMMAND(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_COMMAND_TYPE, GnmCommand))
#define GNM_COMMAND_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST ((k), GNM_COMMAND_TYPE, GnmCommandClass))
#define IS_GNM_COMMAND(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_COMMAND_TYPE))
#define IS_GNM_COMMAND_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), GNM_COMMAND_TYPE))
#define CMD_CLASS(o)		GNM_COMMAND_CLASS (G_OBJECT_GET_CLASS(cmd))

typedef struct {
	GObject parent;
	Sheet *sheet;			/* primary sheet associated with op */
	int size;                       /* See truncate_undo_info.  */
	char const *cmd_descriptor;	/* A string to put in the menu */
} GnmCommand;

typedef gboolean (* UndoCmd)   (GnmCommand *self, WorkbookControl *wbc);
typedef gboolean (* RedoCmd)   (GnmCommand *self, WorkbookControl *wbc);
typedef void	 (* RepeatCmd) (GnmCommand const *orig, WorkbookControl *wbc);

typedef struct {
	GObjectClass parent_class;

	UndoCmd		undo_cmd;
	RedoCmd		redo_cmd;
	RepeatCmd	repeat_cmd;
} GnmCommandClass;

static GSF_CLASS (GnmCommand, gnm_command,
		  NULL, NULL,
		  G_TYPE_OBJECT);

/* Store the real GObject dtor pointer */
static void (* g_object_dtor) (GObject *object) = NULL;

static void
gnm_command_finalize (GObject *obj)
{
	GnmCommand *cmd = GNM_COMMAND (obj);

	g_return_if_fail (cmd != NULL);

	/* The const was to avoid accidental changes elsewhere */
	g_free ((gchar *)cmd->cmd_descriptor);

	/* Call the base class dtor */
	g_return_if_fail (g_object_dtor);
	(*g_object_dtor) (obj);
}

#define MAKE_GNM_COMMAND(type, func, repeat)				\
static gboolean								\
func ## _undo (GnmCommand *me, WorkbookControl *wbc);			\
static gboolean								\
func ## _redo (GnmCommand *me, WorkbookControl *wbc);			\
static void								\
func ## _finalize (GObject *object);					\
static void								\
func ## _class_init (GnmCommandClass *parent)				\
{									\
	parent->undo_cmd   = (UndoCmd)& func ## _undo;			\
	parent->redo_cmd   = (RedoCmd)& func ## _redo;			\
	parent->repeat_cmd = repeat;					\
	if (g_object_dtor == NULL)					\
		g_object_dtor = parent->parent_class.finalize;		\
	parent->parent_class.finalize = & func ## _finalize;		\
}									\
typedef GnmCommandClass type ## Class;					\
static GSF_CLASS (type, func,						\
		  func ## _class_init, NULL, GNM_COMMAND_TYPE);

/******************************************************************/


static char *
make_undo_text (char const *src, int max_len, gboolean *truncated)
{
	char *dst = g_strdup (src);
	char *p;
	int len;

	*truncated = FALSE;
	for (len = 0, p = dst;
	     *p;
	     p = g_utf8_next_char (p), len++) {
		if (len == max_len) {
			*p = 0;
			*truncated = TRUE;
			break;
		}
		if (*p == '\r' || *p == '\n') {
			*p = 0;
			*truncated = TRUE;
			break;
		}
	}

	return dst;
}


/**
 * returns the range name depending on the preference setting
 *
 * char const *undo_global_range_name
 *
 * @pos: GnmCellPos
 *
 * Returns :
 */
static char *
undo_global_range_name (Sheet *sheet, GnmRange const *range)
{
	gboolean show_sheet_name = gnm_app_prefs->show_sheet_name;
	return global_range_name (show_sheet_name ? sheet : NULL, range);
}

/**
 * checks whether the cells are effectively locked
 *
 * static gboolean cmd_cell_range_is_locked_effective
 *
 *
 * Do not use this function unless the sheet is part of the
 * workbook with the given wbc (otherwise the results may be strange)
 */
static gboolean
cmd_cell_range_is_locked_effective (Sheet *sheet, GnmRange *range,
				    WorkbookControl *wbc, char const *cmd_name)
{
	int i, j;
	WorkbookView *wbv = wb_control_view (wbc);

	if (wbv->is_protected || sheet->is_protected)
		for (i = range->start.row; i <= range->end.row; i++)
			for (j = range->start.col; j <= range->end.col; j++)
				if (mstyle_get_content_locked (sheet_style_get (sheet, j, i))) {
					char *text;
					text = g_strdup_printf (wbv->is_protected  ?
				 _("%s is locked. Unprotect the workbook to enable editing.") :
				 _("%s is locked. Unprotect the sheet to enable editing."),
								undo_global_range_name (
									sheet, range));
					gnm_cmd_context_error_invalid (GNM_CMD_CONTEXT (wbc), cmd_name,
								text);
					g_free (text);
					return TRUE;
				}
	return FALSE;
}

/**
 * checks whether the cells are effectively locked
 *
 * static gboolean cmd_dao_is_locked_effective
 *
 *
 * Do not use this function unless the sheet is part of the
 * workbook with the given wbcg (otherwise the results may be strange)
 *
 */

static gboolean
cmd_dao_is_locked_effective (data_analysis_output_t  *dao,
			     WorkbookControl *wbc, char const *cmd_name)
{
	GnmRange range;
	range_init (&range, dao->start_col, dao->start_row,
		    dao->start_col +  dao->cols - 1,  dao->start_row +  dao->rows - 1);
	return (dao->type != NewWorkbookOutput &&
		cmd_cell_range_is_locked_effective (dao->sheet, &range, wbc, cmd_name));
}

/**
 * checks whether the selection is effectively locked
 *
 * static gboolean cmd_selection_is_locked_effective
 *
 *
 * Do not use this function unless the sheet is part of the
 * workbook with the given wbcg (otherwise the results may be strange)
 *
 */

static gboolean
cmd_selection_is_locked_effective (Sheet *sheet, GSList *selection,
				   WorkbookControl *wbc, char const *cmd_name)
{
	for (; selection; selection = selection->next) {
		GnmRange *range = (GnmRange *)selection->data;
		if (cmd_cell_range_is_locked_effective (sheet, range, wbc, cmd_name))
			return TRUE;
	}
	return FALSE;
}

/**
 * returns the cell position name depending on the preference setting
 *
 * char *cmd_cell_pos_name_utility
 *
 * @pos: GnmCellPos
 *
 * Returns :
 */
char *
cmd_cell_pos_name_utility (Sheet *sheet, GnmCellPos const *pos)
{
	GnmRange range;

	range.start = *pos;
	range.end   = *pos;

	return undo_global_range_name (sheet, &range);
}

static guint
max_descriptor_width (void)
{
	return gnm_app_prefs->max_descriptor_width;
}

/**
 * get_menu_label : Utility routine to get the descriptor associated
 *     with a list of commands.
 *
 * @cmd_list : The command list to check.
 *
 * Returns : A static reference to a descriptor.  DO NOT free this.
 */
static char const *
get_menu_label (GSList *cmd_list)
{
	if (cmd_list != NULL) {
		GnmCommand *cmd = GNM_COMMAND (cmd_list->data);
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
		if (workbook_autorecalc (sheet->workbook))
			workbook_recalc (sheet->workbook);
		sheet_update (sheet);

		WORKBOOK_FOREACH_CONTROL (sheet->workbook, view, control,
			  wb_control_sheet_focus (control, sheet);
		);
	}
}

/*
 * cmd_range_list_to_string_utility: Convert a list of ranges into a string.
 *                       (The result will be something like :
 *                        "A1:C3, D4:E5"). The string will be
 *                       automatically truncated to max_descriptor_width ().
 *                       The caller should free the GString that is returned.
 *
 * @ranges : GSList containing GnmRange *'s
 */
GString *
cmd_range_list_to_string_utility (Sheet *sheet, GSList const *ranges)
{
	GString *names;
	GSList const *l;
        char *name;
        guint max_width;

	g_return_val_if_fail (ranges != NULL, NULL);

	names = g_string_new (NULL);
	for (l = ranges; l != NULL; l = l->next) {
		GnmRange const *r = l->data;

                name = undo_global_range_name (sheet , r);
                g_string_append (names, name);
                g_free (name);

		if (l->next)
			g_string_append (names, ", ");
	}

	/* Make sure the string doesn't get overly wide
	 */
	max_width = MAX (3, max_descriptor_width ());
	if (strlen (names->str) > max_width) {
		/* FIXME: this does not look right for UTF8 !!*/
		g_string_truncate (names, max_width - 3);
		g_string_append (names, _("..."));
	}

	return names;
}

char *
cmd_range_to_str_utility (Sheet *sheet, GnmRange const *range)
{
	GSList  *list   = NULL;
	GString *string = NULL;
	char    *text   = NULL;

	list = g_slist_prepend (list, (GnmRange *)range);
	string = cmd_range_list_to_string_utility (sheet, list);
	g_slist_free (list);
	text = string->str;
	g_string_free (string, FALSE);

	return text;
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
	GnmCommand *cmd;
	GnmCommandClass *klass;
	Workbook *wb = wb_control_workbook (wbc);

	g_return_if_fail (wb != NULL);
	g_return_if_fail (wb->undo_commands != NULL);

	cmd = GNM_COMMAND (wb->undo_commands->data);
	g_return_if_fail (cmd != NULL);

	klass = CMD_CLASS (cmd);
	g_return_if_fail (klass != NULL);

	/* TRUE indicates a failure to undo.  Leave the command where it is */
	if (klass->undo_cmd (cmd, wbc))
		return;
	update_after_action (cmd->sheet);

	wb->undo_commands = g_slist_remove (wb->undo_commands,
					    wb->undo_commands->data);
	wb->redo_commands = g_slist_prepend (wb->redo_commands, cmd);

	WORKBOOK_FOREACH_CONTROL (wb, view, control, {
		wb_control_undo_redo_pop (control, TRUE);
		wb_control_undo_redo_push (control, FALSE, cmd->cmd_descriptor, cmd);
	});
	undo_redo_menu_labels (wb);
	/* TODO : Should we mark the workbook as clean or pristine too */
}

/**
 * command_redo : Redo the last command that was undone.
 *
 * @wbc : The workbook control which issued the request.
 *        Any user level errors generated by redoing will be reported
 *        here.
 **/
void
command_redo (WorkbookControl *wbc)
{
	GnmCommand *cmd;
	GnmCommandClass *klass;
	Workbook *wb = wb_control_workbook (wbc);

	g_return_if_fail (wb);
	g_return_if_fail (wb->redo_commands);

	cmd = GNM_COMMAND (wb->redo_commands->data);
	g_return_if_fail (cmd != NULL);

	klass = CMD_CLASS (cmd);
	g_return_if_fail (klass != NULL);

	/* TRUE indicates a failure to redo.  Leave the command where it is */
	if (klass->redo_cmd (cmd, wbc))
		return;
	update_after_action (cmd->sheet);

	/* Remove the command from the undo list */
	wb->redo_commands = g_slist_remove (wb->redo_commands,
					    wb->redo_commands->data);
	wb->undo_commands = g_slist_prepend (wb->undo_commands, cmd);

	WORKBOOK_FOREACH_CONTROL (wb, view, control, {
		wb_control_undo_redo_push (control, TRUE, cmd->cmd_descriptor, cmd);
		wb_control_undo_redo_pop (control, FALSE);
	});
	undo_redo_menu_labels (wb);
}

/**
 * command_repeat : Repeat the last command (if possible)
 *
 * @wbc : The workbook control which issued the request.
 *        Any user level errors generated by redoing will be reported
 *        here.
 **/
void
command_repeat (WorkbookControl *wbc)
{
	GnmCommand *cmd;
	GnmCommandClass *klass;
	Workbook *wb = wb_control_workbook (wbc);

	g_return_if_fail (wb);
	g_return_if_fail (wb->undo_commands);

	cmd = GNM_COMMAND (wb->undo_commands->data);
	g_return_if_fail (cmd != NULL);

	klass = CMD_CLASS (cmd);
	g_return_if_fail (klass != NULL);

	if (klass->repeat_cmd != NULL)
		(*klass->repeat_cmd) (cmd, wbc);
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

	wb_control_undo_redo_truncate (wbc, 0, TRUE);
	tmp = g_slist_reverse (wb->undo_commands);
	for (ptr = tmp ; ptr != NULL ; ptr = ptr->next) {
		undo_label = get_menu_label (ptr);
		wb_control_undo_redo_push (wbc, TRUE, undo_label, ptr->data);
	}
	g_slist_reverse (tmp);

	wb_control_undo_redo_truncate (wbc, 0, FALSE);
	tmp = g_slist_reverse (wb->redo_commands);
	for (ptr = tmp ; ptr != NULL ; ptr = ptr->next) {
		redo_label = get_menu_label (ptr);
		wb_control_undo_redo_push (wbc, FALSE, redo_label, ptr->data);
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
		GObject *cmd = G_OBJECT (cmd_list->data);

		g_return_if_fail (cmd != NULL);

		g_object_unref (cmd);
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
	int size_left;
	int max_num;
	int ok_count;
	GSList *l, *prev;

	size_left = gnm_app_prefs->undo_size;
	max_num   = gnm_app_prefs->undo_max_number;

#ifdef DEBUG_TRUNCATE_UNDO
	fprintf (stderr, "Undo sizes:");
#endif

	for (l = wb->undo_commands, prev = NULL, ok_count = 0;
	     l;
	     prev = l, l = l->next, ok_count++) {
		int min_leave;
		GnmCommand *cmd = GNM_COMMAND (l->data);
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
		if (ok_count >= max_num || (size > size_left && ok_count >= 1)) {
			/* Current item is too big; truncate list here.  */
			command_list_release (l);
			if (prev)
				prev->next = NULL;
			else
				wb->undo_commands = NULL;
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
command_register_undo (WorkbookControl *wbc, GObject *obj)
{
	Workbook *wb;
	GnmCommand *cmd;
	int undo_trunc;

	g_return_if_fail (wbc != NULL);
	wb = wb_control_workbook (wbc);

	cmd = GNM_COMMAND (obj);
	g_return_if_fail (cmd != NULL);

	command_list_release (wb->redo_commands);
	wb->redo_commands = NULL;

	g_object_ref (obj); /* keep a ref in case it gets truncated away */
	wb->undo_commands = g_slist_prepend (wb->undo_commands, cmd);
	undo_trunc = truncate_undo_info (wb);

	WORKBOOK_FOREACH_CONTROL (wb, view, control, {
		wb_control_undo_redo_push (control, TRUE, cmd->cmd_descriptor, cmd);
		if (undo_trunc >= 0)
			wb_control_undo_redo_truncate (control, undo_trunc, TRUE);
		wb_control_undo_redo_truncate (control, 0, FALSE);
	});
	undo_redo_menu_labels (wb);
	g_object_unref (obj);
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
command_push_undo (WorkbookControl *wbc, GObject *obj)
{
	gboolean trouble;
	GnmCommand *cmd;
	GnmCommandClass *klass;

	g_return_val_if_fail (wbc != NULL, TRUE);

	cmd = GNM_COMMAND (obj);
	g_return_val_if_fail (cmd != NULL, TRUE);

	klass = CMD_CLASS (cmd);
	g_return_val_if_fail (klass != NULL, TRUE);

	/* TRUE indicates a failure to do the command */
	trouble = klass->redo_cmd (cmd, wbc);
	update_after_action (cmd->sheet);

	if (!trouble)
		command_register_undo (wbc, obj);
	else
		g_object_unref (obj);

	return trouble;
}

/*
 * command_undo_sheet_delete deletes the sheet without deleting the current cmd.
 * returns true if is indeed deleted the sheet.
 * Note: only call this for a sheet of your current workbook from the undo procedure
 */

static gboolean
command_undo_sheet_delete (Sheet* sheet)
{
	Workbook *wb = sheet->workbook;

        g_return_val_if_fail (IS_SHEET (sheet), FALSE);

	if (wb->redo_commands != NULL) {
		command_list_release (wb->redo_commands);
		wb->redo_commands = NULL;
		WORKBOOK_FOREACH_CONTROL (wb, view, ctl,
			wb_control_undo_redo_truncate (ctl, 0, FALSE););
		undo_redo_menu_labels (wb);
	}

	sheet_deps_destroy (sheet);
	workbook_sheet_detach (wb, sheet, FALSE);

	return (TRUE);
}

static gboolean
command_redo_sheet_delete (Sheet* sheet)
{
	Workbook *wb = sheet->workbook;

        g_return_val_if_fail (IS_SHEET (sheet), FALSE);

	if (wb->undo_commands != NULL) {
		command_list_release (wb->undo_commands);
		wb->undo_commands = NULL;
		WORKBOOK_FOREACH_CONTROL (wb, view, ctl,
					  wb_control_undo_redo_truncate (ctl, 0, TRUE););
		undo_redo_menu_labels (wb);
	}

	sheet_deps_destroy (sheet);
	workbook_sheet_detach (wb, sheet, FALSE);

	return (TRUE);
}

/******************************************************************/

#define CMD_SET_TEXT_TYPE        (cmd_set_text_get_type ())
#define CMD_SET_TEXT(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_SET_TEXT_TYPE, CmdSetText))

typedef struct {
	GnmCommand cmd;

	GnmEvalPos pos;
	gchar		*text;
	PangoAttrList	*markup;
	gboolean	 has_user_format;
	GnmCellRegion	*old_contents;
} CmdSetText;

static void
cmd_set_text_repeat (GnmCommand const *cmd, WorkbookControl *wbc)
{
	CmdSetText const *orig = (CmdSetText const *) cmd;
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	cmd_set_text (wbc, sv_sheet (sv), &sv->edit_pos,
		orig->text, orig->markup);
#warning validation from workbook-edit
}
MAKE_GNM_COMMAND (CmdSetText, cmd_set_text, cmd_set_text_repeat);

static gboolean
cmd_set_text_undo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdSetText *me = CMD_SET_TEXT (cmd);
	GnmRange r;
	GnmPasteTarget pt;

	r.start = r.end = me->pos.eval;
	clipboard_paste_region (me->old_contents,
		paste_target_init (&pt, me->cmd.sheet, &r, PASTE_CONTENT | PASTE_FORMATS),
		GNM_CMD_CONTEXT (wbc));

	return FALSE;
}

static gboolean
cmd_set_text_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdSetText *me = CMD_SET_TEXT (cmd);
	GnmExpr const *expr;
	GnmCell *cell = sheet_cell_fetch (me->pos.sheet,
				       me->pos.eval.col,
				       me->pos.eval.row);
	sheet_cell_set_text (cell, me->text, me->markup);
	expr = cell->base.expression;

	if (!me->has_user_format && expr) {
		GnmEvalPos ep;
		GnmFormat *sf = auto_style_format_suggest (expr,
			eval_pos_init (&ep, me->cmd.sheet, &me->pos.eval));
		if (sf) {
			GnmStyle *new_style = mstyle_new ();
			GnmRange r;

			mstyle_set_format (new_style, sf);
			style_format_unref (sf);
			r.start = r.end = me->pos.eval;
			sheet_apply_style (me->cmd.sheet, &r, new_style);
		}
	}

	return FALSE;
}

static void
cmd_set_text_finalize (GObject *cmd)
{
	CmdSetText *me = CMD_SET_TEXT (cmd);
	if (me->old_contents)
		cellregion_unref (me->old_contents);
	if (me->markup)
		pango_attr_list_unref (me->markup);
	g_free (me->text);
	gnm_command_finalize (cmd);
}

gboolean
cmd_set_text (WorkbookControl *wbc,
	      Sheet *sheet, GnmCellPos const *pos,
	      char const *new_text,
	      PangoAttrList *markup)
{
	GObject *obj;
	CmdSetText *me;
	gchar *text, *corrected_text;
	GnmCell const *cell;
	char *where;
	gboolean truncated;
	GnmRange r;

	g_return_val_if_fail (IS_SHEET (sheet), TRUE);
	g_return_val_if_fail (new_text != NULL, TRUE);

	/* Ensure that we are not splitting up an array */
	cell = sheet_cell_get (sheet, pos->col, pos->row);
	if (cell_is_partial_array (cell)) {
		gnm_cmd_context_error_splits_array (GNM_CMD_CONTEXT (wbc),
					     _("Set Text"), NULL);
		return TRUE;
	}

	corrected_text = autocorrect_tool (new_text);

	obj = g_object_new (CMD_SET_TEXT_TYPE, NULL);
	me = CMD_SET_TEXT (obj);

	me->pos.sheet = sheet;
	me->pos.eval = *pos;
	me->text = corrected_text;
	if (NULL != (me->markup = markup))
		pango_attr_list_ref (me->markup);
	r.start = r.end = *pos;
	me->old_contents = clipboard_copy_range (sheet, &r);

	text = make_undo_text (corrected_text,
			       max_descriptor_width (),
			       &truncated);

	me->cmd.sheet = sheet;
	me->cmd.size = 1;
	where = cmd_cell_pos_name_utility (sheet, pos);
	me->cmd.cmd_descriptor =
		g_strdup_printf (_("Typing \"%s%s\" in %s"),
				 text,
				 truncated ? "..." : "",
				 where);
	g_free (where);
	g_free (text);

	me->has_user_format = !style_format_is_general (
		mstyle_get_format (sheet_style_get (sheet, pos->col, pos->row)));

	return command_push_undo (wbc, obj);
}

/******************************************************************/

#define CMD_AREA_SET_TEXT_TYPE        (cmd_area_set_text_get_type ())
#define CMD_AREA_SET_TEXT(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_AREA_SET_TEXT_TYPE, CmdAreaSetText))

typedef struct {
	GnmCommand cmd;

	GnmParsePos   pp;
	char	  *text;
	gboolean   as_array;
	GSList	*old_content;
	GSList	*selection;
} CmdAreaSetText;

static void
cmd_area_set_text_repeat (GnmCommand const *cmd, WorkbookControl *wbc)
{
	CmdAreaSetText const *orig = (CmdAreaSetText const *) cmd;
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	cmd_area_set_text (wbc, sv,
		orig->text, orig->as_array);
}
MAKE_GNM_COMMAND (CmdAreaSetText, cmd_area_set_text, cmd_area_set_text_repeat);

static gboolean
cmd_area_set_text_undo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdAreaSetText *me = CMD_AREA_SET_TEXT (cmd);
	GSList *ranges;

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->selection != NULL, TRUE);
	g_return_val_if_fail (me->old_content != NULL, TRUE);

	for (ranges = me->selection; ranges != NULL ; ranges = ranges->next) {
		GnmRange const *r = ranges->data;
		GnmCellRegion * c;
		GnmPasteTarget pt;

		g_return_val_if_fail (me->old_content != NULL, TRUE);

		c = me->old_content->data;
		clipboard_paste_region (c,
			paste_target_init (&pt, me->cmd.sheet, r, PASTE_CONTENT | PASTE_FORMATS),
			GNM_CMD_CONTEXT (wbc));
		cellregion_unref (c);
		me->old_content = g_slist_remove (me->old_content, c);
	}
	g_return_val_if_fail (me->old_content == NULL, TRUE);

	return FALSE;
}

static gboolean
cmd_area_set_text_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdAreaSetText *me = CMD_AREA_SET_TEXT (cmd);
	GnmExpr const *expr = NULL;
	GSList *l;
	GnmStyle *new_style = NULL;
	char const *expr_txt;

	g_return_val_if_fail (me != NULL, TRUE);

	/* Check for array subdivision */
	if (sheet_ranges_split_region (me->cmd.sheet, me->selection,
				       GNM_CMD_CONTEXT (wbc), _("Set Text")))
		return TRUE;

	/* Check for locked cells */
	if (cmd_selection_is_locked_effective (me->cmd.sheet, me->selection,
					       wbc, _("Set Text")))
		return TRUE;

	expr_txt = gnm_expr_char_start_p (me->text);
	if (expr_txt != NULL)
		expr = gnm_expr_parse_str_simple (expr_txt, &me->pp);
	if (me->as_array) {
		if (expr == NULL)
			return TRUE;
	} else if (expr != NULL) {
		GnmEvalPos ep;
		GnmFormat *sf = auto_style_format_suggest (expr,
			eval_pos_init (&ep, me->cmd.sheet, &me->pp.eval));
		gnm_expr_unref (expr);
		expr = NULL;
		if (sf != NULL) {
			new_style = mstyle_new ();
			mstyle_set_format (new_style, sf);
			style_format_unref (sf);
		}
	}

	/* Everything is ok. Store previous contents and perform the operation */
	for (l = me->selection ; l != NULL ; l = l->next) {
		GnmRange const *r = l->data;
		me->old_content = g_slist_prepend (me->old_content,
			clipboard_copy_range (me->cmd.sheet, r));

		/* Queue depends of region as a block beforehand */
		sheet_region_queue_recalc (me->cmd.sheet, r);

		/* If there is an expression then this was an array */
		if (expr != NULL) {
			cell_set_array_formula (me->cmd.sheet,
						r->start.col, r->start.row,
						r->end.col, r->end.row,
						expr);
			sheet_region_queue_recalc (me->cmd.sheet, r);
		} else {
			sheet_range_set_text (&me->pp, r, me->text);
			if (new_style) {
				mstyle_ref (new_style);
				sheet_apply_style (me->cmd.sheet, r, new_style);
			}
		}

		/* mark content as dirty */
		sheet_flag_status_update_range (me->cmd.sheet, r);
		sheet_queue_respan (me->cmd.sheet, r->start.row, r->end.row);
	}
	me->old_content = g_slist_reverse (me->old_content);
	sheet_redraw_all (me->cmd.sheet, FALSE);

	if (new_style)
		mstyle_unref (new_style);

	return FALSE;
}

static void
cmd_area_set_text_finalize (GObject *cmd)
{
	CmdAreaSetText *me = CMD_AREA_SET_TEXT (cmd);

	g_free (me->text);

	if (me->old_content != NULL) {
		GSList *l;
		for (l = me->old_content ; l != NULL ; l = g_slist_remove (l, l->data))
			cellregion_unref (l->data);
		me->old_content = NULL;
	}
	range_fragment_free (me->selection);
	me->selection = NULL;

	gnm_command_finalize (cmd);
}

gboolean
cmd_area_set_text (WorkbookControl *wbc, SheetView *sv,
		   char const *new_text, gboolean as_array)
{
#warning add markup
	GObject *obj;
	CmdAreaSetText *me;
	gchar *text;
	gboolean truncated;

	obj = g_object_new (CMD_AREA_SET_TEXT_TYPE, NULL);
	me = CMD_AREA_SET_TEXT (obj);

	me->text        = g_strdup (new_text);
	me->selection   = selection_get_ranges (sv, FALSE /* No intersection */);
	me->old_content = NULL;

	/* Only enter an array formula if
	 *   1) the text is a formula
	 *   2) It's entered as an array formula
	 *   3) There is only one 1 selection
	 */
	me->as_array = (as_array && gnm_expr_char_start_p (me->text) != NULL &&
			me->selection != NULL && me->selection->next == NULL);
	if (me->as_array) {
		/* parse the array expr relative to the top left */
		GnmRange const *r = me->selection->data;
		parse_pos_init (&me->pp, NULL, sv_sheet (sv),
			MIN (r->start.col, r->end.col),
			MIN (r->start.row, r->end.row));
	} else
		parse_pos_init_editpos (&me->pp, sv);

	text = make_undo_text (new_text,
			       max_descriptor_width (),
			       &truncated);

	me->cmd.sheet = me->pp.sheet;
	me->cmd.size = 1;
	me->cmd.cmd_descriptor =
		g_strdup_printf (_("Typing \"%s%s\""),
				 text,
				 truncated ? "..." : "");

	g_free (text);

	return command_push_undo (wbc, obj);
}

/******************************************************************/

#define CMD_INS_DEL_COLROW_TYPE        (cmd_ins_del_colrow_get_type ())
#define CMD_INS_DEL_COLROW(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_INS_DEL_COLROW_TYPE, CmdInsDelColRow))

typedef struct {
	GnmCommand cmd;

	Sheet		*sheet;
	gboolean	 is_insert;
	gboolean	 is_cols;
	gboolean         is_cut;
	int		 index;
	int		 count;
	GnmRange        *cutcopied;
	SheetView	*cut_copy_view;

	ColRowStateList *saved_states;
	GnmCellRegion	*contents;
	GnmRelocUndo	 reloc_storage;
} CmdInsDelColRow;

static void
cmd_ins_del_colrow_repeat (GnmCommand const *cmd, WorkbookControl *wbc)
{
	CmdInsDelColRow const *orig = (CmdInsDelColRow const *) cmd;
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	GnmRange const	*r = selection_first_range (sv,
		GNM_CMD_CONTEXT (wbc), _("Ins/Del Column/Row"));
	if (r == NULL)
		return;
	if (orig->is_insert) {
		if (orig->is_cols)
			cmd_insert_cols (wbc, sv_sheet (sv), r->start.col, range_width (r));
		else
			cmd_insert_rows (wbc, sv_sheet (sv), r->start.row, range_height (r));
	} else {
		if (orig->is_cols)
			cmd_delete_cols (wbc, sv_sheet (sv), r->start.col, range_width (r));
		else
			cmd_delete_rows (wbc, sv_sheet (sv), r->start.row, range_height (r));
	}
}
MAKE_GNM_COMMAND (CmdInsDelColRow, cmd_ins_del_colrow, cmd_ins_del_colrow_repeat);

static gboolean
cmd_ins_del_colrow_undo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdInsDelColRow *me = CMD_INS_DEL_COLROW (cmd);
	int index;
	GnmRelocUndo	tmp;
	gboolean trouble;
	GnmRange r;
	GnmPasteTarget pt;

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->saved_states != NULL, TRUE);
	g_return_val_if_fail (me->contents != NULL, TRUE);

	if (!me->is_insert) {
		index = me->index;
		if (me->is_cols)
			trouble = sheet_insert_cols (me->sheet, me->index, me->count,
						     me->saved_states, &tmp, GNM_CMD_CONTEXT (wbc));
		else
			trouble = sheet_insert_rows (me->sheet, me->index, me->count,
						     me->saved_states, &tmp, GNM_CMD_CONTEXT (wbc));
	} else {
		index = colrow_max (me->is_cols) - me->count;
		if (me->is_cols)
			trouble = sheet_delete_cols (me->sheet, me->index, me->count,
						     me->saved_states, &tmp, GNM_CMD_CONTEXT (wbc));
		else
			trouble = sheet_delete_rows (me->sheet, me->index, me->count,
						     me->saved_states, &tmp, GNM_CMD_CONTEXT (wbc));
	}
	me->saved_states = NULL;

	/* I really do not expect trouble on the undo leg */
	g_return_val_if_fail (!trouble, TRUE);

	/* restore col/row contents */
	if (me->is_cols)
		range_init (&r, index, 0, index+me->count-1, SHEET_MAX_ROWS-1);
	else
		range_init (&r, 0, index, SHEET_MAX_COLS-1, index+me->count-1);

#warning fix object handling
	clipboard_paste_region (me->contents,
		paste_target_init (&pt, me->sheet, &r, PASTE_ALL_TYPES),
		GNM_CMD_CONTEXT (wbc));
	cellregion_unref (me->contents);
	me->contents = NULL;

	/* Throw away the undo info for the expressions after the action*/
	dependents_unrelocate_free (tmp.exprs);

	/* Restore the changed expressions before the action */
	dependents_unrelocate (me->reloc_storage.exprs);
	me->reloc_storage.exprs = NULL;

	/* Ins/Del Row/Col re-ants things completely to account
	 * for the shift of col/rows.
	 */
	if (me->cutcopied != NULL && me->cut_copy_view != NULL)
		gnm_app_clipboard_cut_copy (wbc, me->is_cut, me->cut_copy_view,
						me->cutcopied, FALSE);

	return FALSE;
}

static gboolean
cmd_ins_del_colrow_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdInsDelColRow *me = CMD_INS_DEL_COLROW (cmd);
	GnmRange r;
	gboolean trouble;
	int first, last;

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->saved_states == NULL, TRUE);
	g_return_val_if_fail (me->contents == NULL, TRUE);

	first = (me->is_insert)
		? colrow_max (me->is_cols) - me->count
		: me->index;

	last = first + me->count - 1;
	(me->is_cols)
		? range_init (&r, first, 0, last, SHEET_MAX_ROWS - 1)
		: range_init (&r, 0, first, SHEET_MAX_COLS-1, last);

	/* Check for array subdivision */
	if (!me->is_insert && sheet_range_splits_region
	    (me->sheet, &r, NULL, GNM_CMD_CONTEXT (wbc),
	     (me->is_cols) ? _("Delete Columns") :  _("Delete Rows")))
		return TRUE;

	/* Check for locks */
	if (!me->is_insert && cmd_cell_range_is_locked_effective
	    (me->sheet, &r, wbc, (me->is_cols) ? _("Delete Columns") :  _("Delete Rows")))
		return TRUE;

	me->saved_states = colrow_get_states (me->sheet, me->is_cols, first, last);
	me->contents = clipboard_copy_range (me->sheet, &r);

	if (me->is_insert) {
		ColRowStateList *state = NULL;
		if (me->index > 0) {
			/* Use the size of the preceding _visible_ col/row for
			 * the new one.  If that has default size or this is
			 * the the 1st visible col/row leave the new size as
			 * default.
			 */
			int tmp = colrow_find_adjacent_visible (
				me->sheet, me->is_cols, me->index - 1, FALSE);
			ColRowInfo const *prev_vis = (tmp >= 0)
				? sheet_colrow_get_info (me->sheet, tmp, me->is_cols)
				: sheet_colrow_get_default (me->sheet, me->is_cols);

			/* Use the outline level of the preceding col/row
			 * (visible or not), and leave the new ones visible.
			 */
			ColRowInfo const *prev = sheet_colrow_get_info (
				me->sheet, me->index-1, me->is_cols);

			if (prev->outline_level > 0 || !colrow_is_default (prev_vis))
				state = colrow_make_state (me->sheet, me->count,
					prev_vis->size_pts, prev_vis->hard_size,
					prev->outline_level);
		}

		if (me->is_cols)
			trouble = sheet_insert_cols (me->sheet, me->index, me->count, state,
						     &me->reloc_storage, GNM_CMD_CONTEXT (wbc));
		else
			trouble = sheet_insert_rows (me->sheet, me->index, me->count,
						     state, &me->reloc_storage, GNM_CMD_CONTEXT (wbc));

		if (trouble)
			colrow_state_list_destroy (state);
	} else {
		if (me->is_cols)
			trouble = sheet_delete_cols (me->sheet, me->index, me->count,
						     NULL, &me->reloc_storage, GNM_CMD_CONTEXT (wbc));
		else
			trouble = sheet_delete_rows (me->sheet, me->index, me->count,
						     NULL, &me->reloc_storage, GNM_CMD_CONTEXT (wbc));
	}

	/* Ins/Del Row/Col re-ants things completely to account
	 * for the shift of col/rows.
	 */
	if (!trouble && me->cutcopied != NULL && me->cut_copy_view != NULL) {
		GnmRange s = *me->cutcopied;
		int key = me->is_insert ? me->count : -me->count;
		int threshold = me->is_insert ? me->index : me->index + 1;

		/* Really only applies if the regions that are inserted/
		 * deleted are above the cut/copied region.
		 */
		if (me->is_cols) {
			if (threshold <= s.start.col) {
				s.start.col += key;
				s.end.col   += key;
			}
		} else if (threshold <= s.start.row) {
			s.start.row += key;
			s.end.row   += key;
		}

		gnm_app_clipboard_cut_copy (wbc, me->is_cut, me->cut_copy_view, &s, FALSE);
	}

	return trouble;
}

static void
cmd_ins_del_colrow_finalize (GObject *cmd)
{
	CmdInsDelColRow *me = CMD_INS_DEL_COLROW (cmd);

	if (me->saved_states)
		me->saved_states = colrow_state_list_destroy (me->saved_states);
	if (me->contents) {
		cellregion_unref (me->contents);
		me->contents = NULL;
	}
	if (me->cutcopied) {
		g_free (me->cutcopied);
		me->cutcopied = NULL;
	}
	sv_weak_unref (&(me->cut_copy_view));
	if (me->reloc_storage.exprs) {
		dependents_unrelocate_free (me->reloc_storage.exprs);
		me->reloc_storage.exprs = NULL;
	}
	gnm_command_finalize (cmd);
}

static gboolean
cmd_ins_del_colrow (WorkbookControl *wbc,
		    Sheet *sheet,
		    gboolean is_cols, gboolean is_insert,
		    char const *descriptor, int index, int count)
{
	GObject *obj;
	CmdInsDelColRow *me;

	g_return_val_if_fail (IS_SHEET (sheet), TRUE);

	obj = g_object_new (CMD_INS_DEL_COLROW_TYPE, NULL);
	me = CMD_INS_DEL_COLROW (obj);

	me->sheet = sheet;
	me->is_cols = is_cols;
	me->is_insert = is_insert;
	me->index = index;
	me->count = count;
	me->saved_states = NULL;
	me->contents = NULL;

	/* We store the cut or/copied range if applicable */
	if (!gnm_app_clipboard_is_empty () &&
	    sheet == gnm_app_clipboard_sheet_get ()) {
		me->cutcopied = range_dup (gnm_app_clipboard_area_get ());
		me->is_cut    = gnm_app_clipboard_is_cut ();
		sv_weak_ref (gnm_app_clipboard_sheet_view_get (),
			&(me->cut_copy_view));
	} else
		me->cutcopied = NULL;

	me->cmd.sheet = sheet;
	me->cmd.size = 1;  /* FIXME?  */
	me->cmd.cmd_descriptor = descriptor;

	return command_push_undo (wbc, obj);
}

gboolean
cmd_insert_cols (WorkbookControl *wbc,
		 Sheet *sheet, int start_col, int count)
{
	/* g_strdup_printf does not support positional args, which screws the translators.
	 * We control the buffer content so there is no worry of overflow
	 */
	char mesg[128];
	snprintf (mesg, sizeof (mesg), (count > 1)
		  ? _("Inserting %d columns before %s")
		  : _("Inserting %d column before %s"),
		  count, col_name (start_col));
	return cmd_ins_del_colrow (wbc, sheet, TRUE, TRUE, g_strdup (mesg),
				   start_col, count);
}

gboolean
cmd_insert_rows (WorkbookControl *wbc,
		 Sheet *sheet, int start_row, int count)
{
	/* g_strdup_printf does not support positional args, which screws the translators.
	 * We control the buffer content so there is no worry of overflow
	 */
	char mesg[128];
	snprintf (mesg, sizeof (mesg), (count > 1)
		  ? _("Inserting %d rows before %s")
		  : _("Inserting %d row before %s"),
		  count, row_name (start_row));
	return cmd_ins_del_colrow (wbc, sheet, FALSE, TRUE, g_strdup (mesg),
				   start_row, count);
}

gboolean
cmd_delete_cols (WorkbookControl *wbc,
		 Sheet *sheet, int start_col, int count)
{
	char *mesg = g_strdup_printf ((count > 1)
				      ? _("Deleting columns %s")
				      : _("Deleting column %s"),
				      cols_name (start_col, start_col + count - 1));
	return cmd_ins_del_colrow (wbc, sheet, TRUE, FALSE, mesg, start_col, count);
}

gboolean
cmd_delete_rows (WorkbookControl *wbc,
		 Sheet *sheet, int start_row, int count)
{
	char *mesg = g_strdup_printf ((count > 1)
				      ? _("Deleting rows %s")
				      : _("Deleting row %s"),
				      rows_name (start_row, start_row + count - 1));
	return cmd_ins_del_colrow (wbc, sheet, FALSE, FALSE, mesg, start_row, count);
}

/******************************************************************/

#define CMD_CLEAR_TYPE        (cmd_clear_get_type ())
#define CMD_CLEAR(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_CLEAR_TYPE, CmdClear))

typedef struct {
	GnmCommand cmd;

	int	 clear_flags;
	int	 paste_flags;
	SheetView *sv;
	GSList	  *old_content;
	GSList	  *selection;
} CmdClear;

static void
cmd_clear_repeat (GnmCommand const *cmd, WorkbookControl *wbc)
{
	CmdClear const *orig = (CmdClear const *) cmd;
	cmd_selection_clear (wbc, orig->clear_flags);
}
MAKE_GNM_COMMAND (CmdClear, cmd_clear, cmd_clear_repeat);

static gboolean
cmd_clear_undo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdClear *me = CMD_CLEAR (cmd);
	GSList *ranges;
	SheetView *sv;

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->selection != NULL, TRUE);
	g_return_val_if_fail (me->old_content != NULL, TRUE);

	sv = sheet_get_view (me->cmd.sheet, wb_control_view (wbc));

	/* reset the selection as a convenience AND to queue a redraw */
	sv_selection_reset (sv);

	for (ranges = me->selection; ranges != NULL ; ranges = ranges->next) {
		GnmRange const *r = ranges->data;
		GnmCellRegion  *c;
		GnmPasteTarget pt;

		g_return_val_if_fail (me->old_content != NULL, TRUE);

		c = me->old_content->data;

		if (me->clear_flags)
			clipboard_paste_region (c,
				paste_target_init (&pt, me->cmd.sheet, r, me->paste_flags),
				GNM_CMD_CONTEXT (wbc));

		cellregion_unref (c);
		me->old_content = g_slist_remove (me->old_content, c);
		sv_selection_add_range (sv,
			r->start.col, r->start.row,
			r->start.col, r->start.row,
			r->end.col, r->end.row);
	}
	g_return_val_if_fail (me->old_content == NULL, TRUE);

	return FALSE;
}

static gboolean
cmd_clear_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdClear *me = CMD_CLEAR (cmd);
	GSList *l;

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->selection != NULL, TRUE);
	g_return_val_if_fail (me->old_content == NULL, TRUE);

	/* Check for array subdivision */
	if (sheet_ranges_split_region (me->cmd.sheet, me->selection,
				       GNM_CMD_CONTEXT (wbc), _("Clear")))
		return TRUE;

	/* Check for locked cells */
	if (cmd_selection_is_locked_effective (me->cmd.sheet, me->selection, wbc, _("Clear")))
		return TRUE;

	for (l = me->selection ; l != NULL ; l = l->next) {
		GnmRange const *r = l->data;
		me->old_content =
			g_slist_prepend (me->old_content,
				clipboard_copy_range (me->cmd.sheet, r));

		/* We have already checked the arrays */
		sheet_clear_region (me->cmd.sheet,
			r->start.col, r->start.row, r->end.col, r->end.row,
			me->clear_flags|CLEAR_NOCHECKARRAY|CLEAR_RECALC_DEPS,
			GNM_CMD_CONTEXT (wbc));
	}
	me->old_content = g_slist_reverse (me->old_content);

	return FALSE;
}

static void
cmd_clear_finalize (GObject *cmd)
{
	CmdClear *me = CMD_CLEAR (cmd);

	if (me->old_content != NULL) {
		GSList *l;
		for (l = me->old_content ; l != NULL ; l = g_slist_remove (l, l->data))
			cellregion_unref (l->data);
		me->old_content = NULL;
	}
	range_fragment_free (me->selection);
	me->selection = NULL;

	gnm_command_finalize (cmd);
}

gboolean
cmd_selection_clear (WorkbookControl *wbc, int clear_flags)
{
	GObject *obj;
	CmdClear *me;
	GString *names, *types;
	int paste_flags;
	SheetView *sv = wb_control_cur_sheet_view (wbc);

	paste_flags = 0;
	if (clear_flags & CLEAR_VALUES)
		paste_flags |= PASTE_CONTENT;
	if (clear_flags & CLEAR_FORMATS)
		paste_flags |= PASTE_FORMATS;
	if (clear_flags & CLEAR_COMMENTS)
		paste_flags |= PASTE_COMMENTS;

	obj = g_object_new (CMD_CLEAR_TYPE, NULL);
	me = CMD_CLEAR (obj);

	me->clear_flags = clear_flags;
	me->paste_flags = paste_flags;
	me->old_content = NULL;
	me->selection = selection_get_ranges (sv, FALSE /* No intersection */);

	me->cmd.sheet = sv_sheet (sv);
	me->cmd.size = 1;  /* FIXME?  */

	/* Collect clear types for descriptor */
	if (clear_flags != (CLEAR_VALUES | CLEAR_FORMATS | CLEAR_COMMENTS)) {
		GSList *m, *l = NULL;
		types = g_string_new (NULL);

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

			g_string_append_len (types, s->str, s->len);
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
	names = cmd_range_list_to_string_utility (me->cmd.sheet, me->selection);
	me->cmd.cmd_descriptor = g_strdup_printf (_("Clearing %s in %s"), types->str, names->str);

	g_string_free (names, TRUE);
	g_string_free (types, TRUE);

	return command_push_undo (wbc, obj);
}

/******************************************************************/

#define CMD_FORMAT_TYPE        (cmd_format_get_type ())
#define CMD_FORMAT(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_FORMAT_TYPE, CmdFormat))

typedef struct {
	GnmCellPos pos;
	GnmStyleList *styles;
} CmdFormatOldStyle;

typedef struct {
	GnmCommand cmd;
	GSList	   *selection;
	GSList	   *old_styles;
	GnmStyle   *new_style;
	GnmBorder **borders;
} CmdFormat;

static void
cmd_format_repeat (GnmCommand const *cmd, WorkbookControl *wbc)
{
	CmdFormat const *orig = (CmdFormat const *) cmd;
	int i;

	mstyle_ref (orig->new_style);
	if (orig->borders)
		for (i = STYLE_BORDER_TOP; i < STYLE_BORDER_EDGE_MAX; i++)
			style_border_ref (orig->borders [i]);

	cmd_selection_format (wbc, orig->new_style, orig->borders, NULL);
}
MAKE_GNM_COMMAND (CmdFormat, cmd_format, cmd_format_repeat);

static gboolean
cmd_format_undo (GnmCommand *cmd,
		 G_GNUC_UNUSED WorkbookControl *wbc)
{
	CmdFormat *me = CMD_FORMAT (cmd);

	g_return_val_if_fail (me != NULL, TRUE);

	if (me->old_styles) {
		GSList *l1 = me->old_styles;
		GSList *l2 = me->selection;

		for (; l1; l1 = l1->next, l2 = l2->next) {
			GnmRange const *r;
			CmdFormatOldStyle *os = l1->data;
			SpanCalcFlags flags = sheet_style_set_list (me->cmd.sheet,
				&os->pos, FALSE, os->styles);

			g_return_val_if_fail (l2 && l2->data, TRUE);

			r = l2->data;
			sheet_range_calc_spans (me->cmd.sheet, r, flags);
			if (flags != SPANCALC_SIMPLE)
				rows_height_update (me->cmd.sheet, r, TRUE);
			sheet_flag_format_update_range (me->cmd.sheet, r);
		}
		sheet_redraw_all (me->cmd.sheet, FALSE);
	}

	return FALSE;
}

static gboolean
cmd_format_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdFormat *me = CMD_FORMAT (cmd);
	GSList    *l;

	g_return_val_if_fail (me != NULL, TRUE);

	/* Check for locked cells */
	if (cmd_selection_is_locked_effective (me->cmd.sheet, me->selection,
					       wbc, _("Changing Format")))
		return TRUE;

	for (l = me->selection; l; l = l->next) {
		if (me->borders)
			sheet_style_apply_border (me->cmd.sheet, l->data,
						  me->borders);
		if (me->new_style) {
			mstyle_ref (me->new_style);
			sheet_apply_style (me->cmd.sheet, l->data, me->new_style);
		}
		sheet_flag_format_update_range (me->cmd.sheet, l->data);
	}
	sheet_redraw_all (me->cmd.sheet, FALSE);
	sheet_set_dirty (me->cmd.sheet, TRUE);

	return FALSE;
}

static void
cmd_format_finalize (GObject *cmd)
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

	range_fragment_free (me->selection);
	me->selection = NULL;

	gnm_command_finalize (cmd);
}

/**
 * cmd_format:
 * @wbc: the workbook control.
 * @sheet: the sheet
 * @style: style to apply to the selection
 * @borders: borders to apply to the selection
 * @opt_translated_name : An optional name to use in place of 'Format Cells'
 *
 * If borders is non NULL, then the GnmBorder references are passed,
 * the GnmStyle reference is also passed.
 *
 * It absorbs the reference to the style.
 *
 * Return value: TRUE if there was a problem
 **/
gboolean
cmd_selection_format (WorkbookControl *wbc,
		      GnmStyle *style, GnmBorder **borders,
		      char const *opt_translated_name)
{
	GObject *obj;
	CmdFormat *me;
	GSList    *l;
	SheetView *sv = wb_control_cur_sheet_view (wbc);

	obj = g_object_new (CMD_FORMAT_TYPE, NULL);
	me = CMD_FORMAT (obj);

	me->selection  = selection_get_ranges (sv, FALSE); /* TRUE ? */
	me->new_style  = style;

	me->cmd.sheet = sv_sheet (sv);
	me->cmd.size = 1;  /* Updated below.  */

	me->old_styles = NULL;
	for (l = me->selection; l; l = l->next) {
		CmdFormatOldStyle *os;
		GnmRange range = *((GnmRange const *)l->data);

		/* Store the containing range to handle borders */
		if (borders != NULL) {
			if (range.start.col > 0) range.start.col--;
			if (range.start.row > 0) range.start.row--;
			if (range.end.col < SHEET_MAX_COLS-1) range.end.col++;
			if (range.end.row < SHEET_MAX_ROWS-1) range.end.row++;
		}

		os = g_new (CmdFormatOldStyle, 1);

		os->styles = sheet_style_get_list (me->cmd.sheet, &range);
		os->pos = range.start;

		me->cmd.size += g_slist_length (os->styles);
		me->old_styles = g_slist_append (me->old_styles, os);
	}

	if (borders) {
		int i;

		me->borders = g_new (GnmBorder *, STYLE_BORDER_EDGE_MAX);
		for (i = STYLE_BORDER_TOP; i < STYLE_BORDER_EDGE_MAX; i++)
			me->borders [i] = borders [i];
	} else
		me->borders = NULL;

	if (opt_translated_name == NULL) {
		GString *names = cmd_range_list_to_string_utility (me->cmd.sheet,
								   me->selection);

		me->cmd.cmd_descriptor = g_strdup_printf (_("Changing format of %s"), names->str);
		g_string_free (names, TRUE);
	} else
		me->cmd.cmd_descriptor = g_strdup (opt_translated_name);

	return command_push_undo (wbc, obj);
}

/******************************************************************/

#define CMD_RESIZE_COLROW_TYPE        (cmd_resize_colrow_get_type ())
#define CMD_RESIZE_COLROW(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_RESIZE_COLROW_TYPE, CmdResizeColRow))

typedef struct {
	GnmCommand cmd;

	Sheet		*sheet;
	gboolean	 is_cols;
	ColRowIndexList *selection;
	ColRowStateGroup*saved_sizes;
	int		 new_size;
} CmdResizeColRow;

MAKE_GNM_COMMAND (CmdResizeColRow, cmd_resize_colrow, NULL);

static gboolean
cmd_resize_colrow_undo (GnmCommand *cmd,
			G_GNUC_UNUSED WorkbookControl *wbc)
{
	CmdResizeColRow *me = CMD_RESIZE_COLROW (cmd);

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->selection != NULL, TRUE);
	g_return_val_if_fail (me->saved_sizes != NULL, TRUE);

	colrow_restore_state_group (me->sheet, me->is_cols,
				    me->selection, me->saved_sizes);
	me->saved_sizes = NULL;

	return FALSE;
}

static gboolean
cmd_resize_colrow_redo (GnmCommand *cmd,
			G_GNUC_UNUSED WorkbookControl *wbc)
{
	CmdResizeColRow *me = CMD_RESIZE_COLROW (cmd);

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->selection != NULL, TRUE);
	g_return_val_if_fail (me->saved_sizes == NULL, TRUE);

	me->saved_sizes = colrow_set_sizes (me->sheet, me->is_cols,
					    me->selection, me->new_size);
	if (me->cmd.size == 1)
		me->cmd.size += (g_slist_length (me->saved_sizes) +
				 g_list_length (me->selection));

	return FALSE;
}
static void
cmd_resize_colrow_finalize (GObject *cmd)
{
	CmdResizeColRow *me = CMD_RESIZE_COLROW (cmd);

	if (me->selection)
		me->selection = colrow_index_list_destroy (me->selection);

	if (me->saved_sizes)
		me->saved_sizes = colrow_state_group_destroy (me->saved_sizes);

	gnm_command_finalize (cmd);
}

gboolean
cmd_resize_colrow (WorkbookControl *wbc, Sheet *sheet,
		   gboolean is_cols, ColRowIndexList *selection,
		   int new_size)
{
	GObject *obj;
	CmdResizeColRow *me;
	GString *list;
	gboolean is_single;
	guint max_width;

	g_return_val_if_fail (IS_SHEET (sheet), TRUE);

	obj = g_object_new (CMD_RESIZE_COLROW_TYPE, NULL);
	me = CMD_RESIZE_COLROW (obj);

	me->sheet = sheet;
	me->is_cols = is_cols;
	me->selection = selection;
	me->saved_sizes = NULL;
	me->new_size = new_size;

	me->cmd.sheet = sheet;
	me->cmd.size = 1;  /* Changed in initial redo.  */

	list = colrow_index_list_to_string (selection, is_cols, &is_single);
	/* Make sure the string doesn't get overly wide */
	max_width = max_descriptor_width ();
	if (strlen (list->str) > max_width) {
		g_string_truncate (list, max_width - 3);
		g_string_append (list, "...");
	}

	if (is_single) {
		if (new_size < 0)
			me->cmd.cmd_descriptor = is_cols
				? g_strdup_printf (_("Autofitting column %s"), list->str)
				: g_strdup_printf (_("Autofitting row %s"), list->str);
		else if (new_size >  0)
			me->cmd.cmd_descriptor = is_cols
				? g_strdup_printf (_("Setting width of column %s to %d pixels"),
						   list->str, new_size)
				: g_strdup_printf (_("Setting height of row %s to %d pixels"),
						   list->str, new_size);
		else me->cmd.cmd_descriptor = is_cols
			     ? g_strdup_printf (_("Setting width of column %s to default"),
						list->str)
			     : g_strdup_printf (
				     _("Setting height of row %s to default"), list->str);
	} else {
		if (new_size < 0)
			me->cmd.cmd_descriptor = is_cols
				? g_strdup_printf (_("Autofitting columns %s"), list->str)
				: g_strdup_printf (_("Autofitting rows %s"), list->str);
		else if (new_size >  0)
			me->cmd.cmd_descriptor = is_cols
				? g_strdup_printf (_("Setting width of columns %s to %d pixels"),
						   list->str, new_size)
				: g_strdup_printf (_("Setting height of rows %s to %d pixels"),
						   list->str, new_size);
		else me->cmd.cmd_descriptor = is_cols
			     ? g_strdup_printf (
				     _("Setting width of columns %s to default"), list->str)
			     : g_strdup_printf (
				     _("Setting height of rows %s to default"), list->str);
	}

	g_string_free (list, TRUE);
	return command_push_undo (wbc, obj);
}

/******************************************************************/

#define CMD_SORT_TYPE        (cmd_sort_get_type ())
#define CMD_SORT(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_SORT_TYPE, CmdSort))

typedef struct {
	GnmCommand cmd;

	GnmSortData *data;
	int         *perm;
	int         *inv;
} CmdSort;

MAKE_GNM_COMMAND (CmdSort, cmd_sort, NULL);

static void
cmd_sort_finalize (GObject *cmd)
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

	gnm_command_finalize (cmd);
}

static gboolean
cmd_sort_undo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdSort *me = CMD_SORT (cmd);
	g_return_val_if_fail (me != NULL, TRUE);

	if (!me->inv) {
		me->inv = sort_permute_invert (me->perm, sort_data_length (me->data));
	}
	sort_position (me->data, me->inv, GNM_CMD_CONTEXT (wbc));

	return FALSE;
}

static gboolean
cmd_sort_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdSort *me = CMD_SORT (cmd);

	g_return_val_if_fail (me != NULL, TRUE);

	/* Check for locks */
	if (cmd_cell_range_is_locked_effective
	    (me->data->sheet, me->data->range, wbc, _("Sorting")))
		return TRUE;

	if (!me->perm) {
		me->perm = sort_contents (me->data, GNM_CMD_CONTEXT (wbc));
		me->cmd.size += 2 * sort_data_length (me->data);
	} else
		sort_position (me->data, me->perm, GNM_CMD_CONTEXT (wbc));

	return FALSE;
}
gboolean
cmd_sort (WorkbookControl *wbc, GnmSortData *data)
{
	GObject *obj;
	CmdSort *me;
	char *desc;

	g_return_val_if_fail (data != NULL, TRUE);

	desc = g_strdup_printf (_("Sorting %s"), range_name (data->range));
	if (sheet_range_contains_region (data->sheet, data->range, GNM_CMD_CONTEXT (wbc), desc)) {
		sort_data_destroy (data);
		g_free (desc);
		return TRUE;
	}

	obj = g_object_new (CMD_SORT_TYPE, NULL);
	me = CMD_SORT (obj);

	me->data = data;
	me->perm = NULL;
	me->inv = NULL;

	me->cmd.sheet = data->sheet;
	me->cmd.size = 1;  /* Changed in initial redo.  */
	me->cmd.cmd_descriptor = desc;

	return command_push_undo (wbc, obj);
}

/******************************************************************/

#define CMD_COLROW_HIDE_TYPE        (cmd_colrow_hide_get_type ())
#define CMD_COLROW_HIDE(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_COLROW_HIDE_TYPE, CmdColRowHide))

typedef struct {
	GnmCommand cmd;

	gboolean       is_cols;
	ColRowVisList *hide, *show;
} CmdColRowHide;

static void
cmd_colrow_hide_repeat (GnmCommand const *cmd, WorkbookControl *wbc)
{
	CmdColRowHide const *orig = (CmdColRowHide const *) cmd;
	cmd_selection_colrow_hide (wbc, orig->is_cols, orig->show != NULL);
}
MAKE_GNM_COMMAND (CmdColRowHide, cmd_colrow_hide, cmd_colrow_hide_repeat);

/**
 * cmd_colrow_hide_correct_selection :
 *
 * Try to ensure that the selection/cursor is set to a visible row/col
 *
 * Added to fix bug 38179
 * Removed because the result is irritating and the bug is actually XL
 * compatibile
 **/
static void
cmd_colrow_hide_correct_selection (CmdColRowHide *me, WorkbookControl *wbc)
{
#if 0
	int x, y, index;
	SheetView *sv = sheet_get_view (me->cmd.sheet,
		wb_control_view (wbc));

	index = colrow_find_adjacent_visible (me->cmd.sheet, me->is_cols,
			me->is_cols ? sv->edit_pos.col : sv->edit_pos.row,
			TRUE);

	x = me->is_cols ? sv->edit_pos.row : index;
	y = me->is_cols ? index : sv->edit_pos.col;

	if (index >= 0) {
		sv_selection_reset (sv);
		if (me->is_cols)
			sv_selection_add_range (sv, y, x, y, 0,
						y, SHEET_MAX_ROWS - 1);
		else
			sv_selection_add_range (sv, y, x, 0, x,
						SHEET_MAX_COLS - 1, x);
	}
#endif
}

static gboolean
cmd_colrow_hide_undo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdColRowHide *me = CMD_COLROW_HIDE (cmd);

	g_return_val_if_fail (me != NULL, TRUE);

	colrow_set_visibility_list (me->cmd.sheet, me->is_cols,
				    TRUE, me->hide);
	colrow_set_visibility_list (me->cmd.sheet, me->is_cols,
				    FALSE, me->show);

	if (me->show != NULL)
		cmd_colrow_hide_correct_selection (me, wbc);

	return FALSE;
}

static gboolean
cmd_colrow_hide_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdColRowHide *me = CMD_COLROW_HIDE (cmd);

	g_return_val_if_fail (me != NULL, TRUE);

	colrow_set_visibility_list (me->cmd.sheet, me->is_cols,
				    FALSE, me->hide);
	colrow_set_visibility_list (me->cmd.sheet, me->is_cols,
				    TRUE, me->show);

	if (me->hide != NULL)
		cmd_colrow_hide_correct_selection (me, wbc);

	return FALSE;
}

static void
cmd_colrow_hide_finalize (GObject *cmd)
{
	CmdColRowHide *me = CMD_COLROW_HIDE (cmd);
	me->hide = colrow_vis_list_destroy (me->hide);
	me->show = colrow_vis_list_destroy (me->show);
	gnm_command_finalize (cmd);
}

gboolean
cmd_selection_colrow_hide (WorkbookControl *wbc,
			   gboolean is_cols, gboolean visible)
{
	GObject *obj;
	CmdColRowHide *me;
	SheetView *sv = wb_control_cur_sheet_view (wbc);

	obj = g_object_new (CMD_COLROW_HIDE_TYPE, NULL);
	me = CMD_COLROW_HIDE (obj);

	me->is_cols = is_cols;
	me->hide = me->show = NULL;
	if (visible)
		me->show = colrow_get_visiblity_toggle (sv, is_cols, TRUE);
	else
		me->hide = colrow_get_visiblity_toggle (sv, is_cols, FALSE);

	me->cmd.sheet = sv_sheet (sv);
	me->cmd.size = 1 + g_slist_length (me->hide) + g_slist_length (me->show);
	me->cmd.cmd_descriptor = g_strdup (is_cols
		? (visible ? _("Unhide columns") : _("Hide columns"))
		: (visible ? _("Unhide rows") : _("Hide rows")));

	return command_push_undo (wbc, obj);
}

gboolean
cmd_selection_outline_change (WorkbookControl *wbc,
			      gboolean is_cols, int index, int depth)
{
	GObject *obj;
	CmdColRowHide *me;
	ColRowInfo const *cri;
	int first = -1, last = -1;
	gboolean visible = FALSE;
	int d;
	Sheet	  *sheet = wb_control_cur_sheet (wbc);
	SheetView *sv	 = wb_control_cur_sheet_view (wbc);

	cri = sheet_colrow_get_info (sheet, index, is_cols);

	d = cri->outline_level;
	if (depth > d)
		depth = d;

	/* Nodes only collapse when selected directly, selecting at a lower
	 * level is a standard toggle. */
	if (depth == d) {
		if ((is_cols ? sheet->outline_symbols_right : sheet->outline_symbols_below)) {
			if (index > 0) {
				ColRowInfo const *prev =
					sheet_colrow_get (sheet, index-1, is_cols);

				if (prev != NULL && prev->outline_level > d) {
					visible = (depth == d && cri->is_collapsed);
					last = index - 1;
					first = colrow_find_outline_bound (sheet, is_cols,
						last, d+1, FALSE);
				}
			}
		} else if (index+1 < colrow_max (is_cols)) {
			ColRowInfo const *next =
				sheet_colrow_get (sheet, index+1, is_cols);

			if (next != NULL && next->outline_level > d) {
				visible = (depth == d && cri->is_collapsed);
				first = index + 1;
				last = colrow_find_outline_bound (sheet, is_cols,
					first, d+1, TRUE);
			}
		}
	}

	/* If nothing done yet do a simple collapse */
	if (first < 0 && cri->outline_level > 0) {
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

	obj = g_object_new (CMD_COLROW_HIDE_TYPE, NULL);
	me = CMD_COLROW_HIDE (obj);

	me->is_cols = is_cols;
	me->hide = me->show = NULL;
	if (visible)
		me->show = colrow_get_outline_toggle (sv_sheet (sv), is_cols,
						      TRUE, first, last);
	else
		me->hide = colrow_get_outline_toggle (sv_sheet (sv), is_cols,
						      FALSE, first, last);

	me->cmd.sheet = sv_sheet (sv);
	me->cmd.size = 1 + g_slist_length (me->show) + g_slist_length (me->hide);
	me->cmd.cmd_descriptor = g_strdup (is_cols
		? (visible ? _("Expand columns") : _("Collapse columns"))
		: (visible ? _("Expand rows") : _("Collapse rows")));

	return command_push_undo (wbc, obj);
}

gboolean
cmd_global_outline_change (WorkbookControl *wbc, gboolean is_cols, int depth)
{
	GObject *obj;
	CmdColRowHide *me;
	ColRowVisList *hide, *show;
	SheetView *sv	 = wb_control_cur_sheet_view (wbc);

	colrow_get_global_outline (sv_sheet (sv), is_cols, depth, &show, &hide);

	if (show == NULL && hide == NULL)
		return TRUE;

	obj = g_object_new (CMD_COLROW_HIDE_TYPE, NULL);
	me = CMD_COLROW_HIDE (obj);
	me->is_cols	= is_cols;
	me->hide	= hide;
	me->show	= show;
	me->cmd.sheet	= sv_sheet (sv);
	me->cmd.size = 1 + g_slist_length (me->show) + g_slist_length (me->hide);
	me->cmd.cmd_descriptor = g_strdup_printf (is_cols
		? _("Show column outline %d") : _("Show row outline %d"), depth);

	return command_push_undo (wbc, obj);
}

/******************************************************************/

#define CMD_GROUP_TYPE        (cmd_group_get_type ())
#define CMD_GROUP(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_GROUP_TYPE, CmdGroup))

typedef struct {
	GnmCommand cmd;

	GnmRange       range;
	gboolean       is_cols;
	gboolean       group;
} CmdGroup;

static void
cmd_group_repeat (GnmCommand const *cmd, WorkbookControl *wbc)
{
	CmdGroup const *orig = (CmdGroup const *) cmd;
	cmd_selection_group (wbc, orig->is_cols, orig->group);
}
MAKE_GNM_COMMAND (CmdGroup, cmd_group, cmd_group_repeat);

static gboolean
cmd_group_undo (GnmCommand *cmd,
		G_GNUC_UNUSED WorkbookControl *wbc)
{
	CmdGroup const *me = CMD_GROUP (cmd);
	sheet_colrow_group_ungroup (me->cmd.sheet,
		&me->range, me->is_cols, !me->group);
	return FALSE;
}

static gboolean
cmd_group_redo (GnmCommand *cmd,
		G_GNUC_UNUSED WorkbookControl *wbc)
{
	CmdGroup const *me = CMD_GROUP (cmd);
	sheet_colrow_group_ungroup (me->cmd.sheet,
		&me->range, me->is_cols, me->group);
	return FALSE;
}

static void
cmd_group_finalize (GObject *cmd)
{
	gnm_command_finalize (cmd);
}

gboolean
cmd_selection_group (WorkbookControl *wbc,
		     gboolean is_cols, gboolean group)
{
	GObject   *obj;
	CmdGroup  *me;
	SheetView *sv;
	GnmRange	   r;

	g_return_val_if_fail (wbc != NULL, TRUE);

	sv = wb_control_cur_sheet_view (wbc);
	r = *selection_first_range (sv, NULL, NULL);

	/* Check if this really is possible and display an error if it's not */
	if (sheet_colrow_can_group (sv->sheet, &r, is_cols) != group) {
		if (group) {
			gnm_cmd_context_error_system (GNM_CMD_CONTEXT (wbc), is_cols
					       ? _("Those columns are already grouped")
					       : _("Those rows are already grouped"));
			return TRUE;
		}

		/* see if the user selected the col/row with the marker too */
		if (is_cols) {
			if (r.start.col != r.end.col) {
				if (sv->sheet->outline_symbols_right)
					r.end.col--;
				else
					r.start.col++;
			}
		} else {
			if (r.start.row != r.end.row) {
				if (sv->sheet->outline_symbols_below)
					r.end.row--;
				else
					r.start.row++;
			}
		}

		if (sheet_colrow_can_group (sv->sheet, &r, is_cols) != group) {
			gnm_cmd_context_error_system (GNM_CMD_CONTEXT (wbc), is_cols
					       ? _("Those columns are not grouped, you can't ungroup them")
					       : _("Those rows are not grouped, you can't ungroup them"));
			return TRUE;
		}
	}

	obj = g_object_new (CMD_GROUP_TYPE, NULL);
	me = CMD_GROUP (obj);
	me->is_cols = is_cols;
	me->group = group;
	me->range = r;

	me->cmd.sheet = sv->sheet;
	me->cmd.size = 1;
	me->cmd.cmd_descriptor = is_cols
		? g_strdup_printf (group ? _("Group columns %s") : _("Ungroup columns %s"),
				   cols_name (me->range.start.col, me->range.end.col))
		: g_strdup_printf (group ? _("Group rows %d:%d") : _("Ungroup rows %d:%d"),
				   me->range.start.row + 1, me->range.end.row + 1);

	return command_push_undo (wbc, obj);
}

/******************************************************************/

#define CMD_PASTE_CUT_TYPE        (cmd_paste_cut_get_type ())
#define CMD_PASTE_CUT(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_PASTE_CUT_TYPE, CmdPasteCut))

typedef struct {
	GnmCommand cmd;

	GnmExprRelocateInfo info;
	GSList		*paste_content;
	GnmRelocUndo	 reloc_storage;
	gboolean	 move_selection;
	ColRowStateList *saved_sizes;

	/* handle redo-ing an undo with content from a deleted sheet */
	GnmCellRegion *deleted_sheet_contents;
} CmdPasteCut;

MAKE_GNM_COMMAND (CmdPasteCut, cmd_paste_cut, NULL);

typedef struct {
	GnmPasteTarget pt;
	GnmCellRegion *contents;
} PasteContent;

/**
 * cmd_paste_cut_update_origin :
 *
 * Utility routine to update things when we are transfering between sheets and
 * workbooks.
 */
static void
cmd_paste_cut_update_origin (GnmExprRelocateInfo const  *info,
			     G_GNUC_UNUSED WorkbookControl *wbc)
{
	/* Dirty and update both sheets */
	if (info->origin_sheet != info->target_sheet) {
		sheet_set_dirty (info->target_sheet, TRUE);

		/* An if necessary both workbooks */
		if (IS_SHEET (info->origin_sheet) &&
		    info->origin_sheet->workbook != info->target_sheet->workbook) {
			if (workbook_autorecalc (info->origin_sheet->workbook))
				workbook_recalc (info->origin_sheet->workbook);
			sheet_update (info->origin_sheet);
		}
	}
}

static gboolean
cmd_paste_cut_undo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdPasteCut *me = CMD_PASTE_CUT (cmd);
	GnmExprRelocateInfo reverse;

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->paste_content != NULL, TRUE);
	g_return_val_if_fail (me->deleted_sheet_contents == NULL, TRUE);

	reverse.target_sheet = me->info.origin_sheet;
	reverse.origin_sheet = me->info.target_sheet;
	reverse.origin = me->info.origin;
	range_translate (&reverse.origin,
			 me->info.col_offset,
			 me->info.row_offset);
	reverse.col_offset = -me->info.col_offset;
	reverse.row_offset = -me->info.row_offset;

	/* Move things back being careful NOT to invalidate the src region */
	if (IS_SHEET (me->info.origin_sheet))
		sheet_move_range (&reverse, NULL, GNM_CMD_CONTEXT (wbc));
	else
		me->deleted_sheet_contents = clipboard_copy_range (
			reverse.origin_sheet, &reverse.origin);

	/* Restore the original row heights */
	colrow_set_states (me->info.target_sheet, FALSE,
		reverse.origin.start.row, me->saved_sizes);
	colrow_state_list_destroy (me->saved_sizes);
	me->saved_sizes = NULL;

	/* Restore the changed expressions */
	dependents_unrelocate (me->reloc_storage.exprs);
	me->reloc_storage.exprs = NULL;

	while (me->paste_content) {
		PasteContent *pc = me->paste_content->data;
		me->paste_content = g_slist_remove (me->paste_content, pc);

		clipboard_paste_region (pc->contents, &pc->pt, GNM_CMD_CONTEXT (wbc));
		cellregion_unref (pc->contents);
		g_free (pc);
	}

	/* Force update of the status area */
	sheet_flag_status_update_range (me->info.target_sheet, NULL);

	/* Select the original region */
	if (me->move_selection && IS_SHEET (me->info.origin_sheet)) {
		SheetView *sv = sheet_get_view (me->info.origin_sheet,
			wb_control_view (wbc));
		if (sv != NULL)
			sv_selection_set (sv,
				  &me->info.origin.start,
				  me->info.origin.start.col,
				  me->info.origin.start.row,
				  me->info.origin.end.col,
				  me->info.origin.end.row);
	}

	cmd_paste_cut_update_origin (&me->info, wbc);

	return FALSE;
}

static gboolean
cmd_paste_cut_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdPasteCut *me = CMD_PASTE_CUT (cmd);
	GnmRange  tmp;

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->paste_content == NULL, TRUE);
	g_return_val_if_fail (me->reloc_storage.exprs == NULL, TRUE);

	tmp = me->info.origin;
	range_translate (&tmp, me->info.col_offset, me->info.row_offset);
	range_normalize (&tmp);

	g_return_val_if_fail (range_is_sane (&tmp), TRUE);

	if (me->info.origin_sheet != me->info.target_sheet ||
	    !range_overlap (&me->info.origin, &tmp)) {
		PasteContent *pc = g_new (PasteContent, 1);
		paste_target_init (&pc->pt, me->info.target_sheet, &tmp, PASTE_ALL_TYPES);
		pc->contents = clipboard_copy_range (me->info.target_sheet, &tmp);
		me->paste_content = g_slist_prepend (me->paste_content, pc);
	} else {
		/* need to store any portions of the paste target
		 * that do not overlap with the source.
		 */
		GSList *ptr, *frag = range_split_ranges (&me->info.origin, &tmp);
		for (ptr = frag ; ptr != NULL ; ptr = ptr->next) {
			GnmRange *r = ptr->data;

			if (!range_overlap (&me->info.origin, r)) {
				PasteContent *pc = g_new (PasteContent, 1);
				paste_target_init (&pc->pt, me->info.target_sheet, r, PASTE_ALL_TYPES);
				pc->contents = clipboard_copy_range (me->info.target_sheet,  r);
				me->paste_content = g_slist_prepend (me->paste_content, pc);
			}
			g_free (r);
		}
		g_slist_free (frag);
	}

	/* rare corner case.  If the origin sheet has been deleted */
	if (!IS_SHEET (me->info.origin_sheet)) {
		GnmPasteTarget pt;
		paste_target_init (&pt, me->info.target_sheet, &tmp, PASTE_ALL_TYPES);
		sheet_clear_region (pt.sheet,
			tmp.start.col, tmp.start.row, tmp.end.col,   tmp.end.row,
			CLEAR_VALUES | CLEAR_MERGES | CLEAR_NOCHECKARRAY | CLEAR_RECALC_DEPS,
			GNM_CMD_CONTEXT (wbc));
		clipboard_paste_region (me->deleted_sheet_contents,
			&pt, GNM_CMD_CONTEXT (wbc));
		cellregion_unref (me->deleted_sheet_contents);
		me->deleted_sheet_contents = NULL;
	} else
		sheet_move_range (&me->info, &me->reloc_storage, GNM_CMD_CONTEXT (wbc));

	cmd_paste_cut_update_origin (&me->info, wbc);

	/* Backup row heights and adjust row heights to fit */
	me->saved_sizes = colrow_get_states (me->info.target_sheet, FALSE, tmp.start.row, tmp.end.row);
	rows_height_update (me->info.target_sheet, &tmp, FALSE);

	/* Make sure the destination is selected */
	if (me->move_selection)
		sv_selection_set (sheet_get_view (me->info.target_sheet, wb_control_view (wbc)),
				  &tmp.start,
				  tmp.start.col, tmp.start.row,
				  tmp.end.col, tmp.end.row);

	return FALSE;
}

static void
cmd_paste_cut_finalize (GObject *cmd)
{
	CmdPasteCut *me = CMD_PASTE_CUT (cmd);

	if (me->saved_sizes)
		me->saved_sizes = colrow_state_list_destroy (me->saved_sizes);
	while (me->paste_content) {
		PasteContent *pc = me->paste_content->data;
		me->paste_content = g_slist_remove (me->paste_content, pc);
		cellregion_unref (pc->contents);
		g_free (pc);
	}
	if (me->reloc_storage.exprs) {
		dependents_unrelocate_free (me->reloc_storage.exprs);
		me->reloc_storage.exprs = NULL;
	}
	if (me->deleted_sheet_contents) {
		cellregion_unref (me->deleted_sheet_contents);
		me->deleted_sheet_contents = NULL;
	}

	gnm_command_finalize (cmd);
}

gboolean
cmd_paste_cut (WorkbookControl *wbc, GnmExprRelocateInfo const *info,
	       gboolean move_selection, char *descriptor)
{
	GObject *obj;
	CmdPasteCut *me;
	GnmRange r;
	char *where;

	g_return_val_if_fail (info != NULL, TRUE);

	/* This is vacuous */
	if (info->origin_sheet == info->target_sheet &&
	    info->col_offset == 0 && info->row_offset == 0)
		return TRUE;

	/* FIXME: Do we want to show the destination range as well ? */
	where = undo_global_range_name (info->origin_sheet, &info->origin);
	if (descriptor == NULL)
		descriptor = g_strdup_printf (_("Moving %s"), where);
	g_free (where);

	g_return_val_if_fail (info != NULL, TRUE);

	r = info->origin;
	if (range_translate (&r, info->col_offset, info->row_offset)) {

		gnm_cmd_context_error_invalid (GNM_CMD_CONTEXT (wbc), descriptor,
					_("is beyond sheet boundaries"));
		g_free (descriptor);
		return TRUE;
	}

	/* Check array subdivision & merged regions */
	if (sheet_range_splits_region (info->target_sheet, &r,
		(info->origin_sheet == info->target_sheet)
		? &info->origin : NULL, GNM_CMD_CONTEXT (wbc), descriptor)) {
		g_free (descriptor);
		return TRUE;
	}

	obj = g_object_new (CMD_PASTE_CUT_TYPE, NULL);
	me = CMD_PASTE_CUT (obj);

	me->info = *info;
	me->paste_content  = NULL;
	me->deleted_sheet_contents = NULL;
	me->reloc_storage.exprs  = NULL;
	me->move_selection = move_selection;
	me->saved_sizes    = NULL;

	me->cmd.sheet = info->target_sheet;
	me->cmd.size = 1;  /* FIXME?  */
	me->cmd.cmd_descriptor = descriptor;

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

	return command_push_undo (wbc, obj);
}

/******************************************************************/

#define CMD_PASTE_COPY_TYPE        (cmd_paste_copy_get_type ())
#define CMD_PASTE_COPY(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_PASTE_COPY_TYPE, CmdPasteCopy))

typedef struct {
	GnmCommand cmd;

	GnmCellRegion   *content;
	GnmPasteTarget   dst;
	gboolean         has_been_through_cycle;
	ColRowStateList *saved_sizes;
} CmdPasteCopy;

static void
cmd_paste_copy_repeat (GnmCommand const *cmd, WorkbookControl *wbc)
{
	CmdPasteCopy const *orig = (CmdPasteCopy const *) cmd;
	GnmPasteTarget  new_dst;
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	GnmRange const	*r = selection_first_range (sv,
		GNM_CMD_CONTEXT (wbc), _("Paste Copy"));

	if (r == NULL)
		return;

	paste_target_init (&new_dst, sv_sheet (sv), r, orig->dst.paste_flags);
	cmd_paste_copy (wbc, &new_dst,
		clipboard_copy_range (orig->dst.sheet, &orig->dst.range));
}
MAKE_GNM_COMMAND (CmdPasteCopy, cmd_paste_copy, cmd_paste_copy_repeat);

static gboolean
cmd_paste_copy_impl (GnmCommand *cmd, WorkbookControl *wbc,
		     gboolean is_undo)
{
	CmdPasteCopy *me = CMD_PASTE_COPY (cmd);
	GnmCellRegion *content;
	SheetView *sv;

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->content != NULL, TRUE);

	content = clipboard_copy_range (me->dst.sheet, &me->dst.range);
	if (clipboard_paste_region (me->content, &me->dst, GNM_CMD_CONTEXT (wbc))) {
		/* There was a problem, avoid leaking */
		cellregion_unref (content);
		return TRUE;
	}

	if (me->has_been_through_cycle)
		cellregion_unref (me->content);
	else
		/* Save the content */
		me->dst.paste_flags = PASTE_CONTENT |
			(me->dst.paste_flags & PASTE_FORMATS);

	if (is_undo) {
		colrow_set_states (me->dst.sheet, FALSE,
			me->dst.range.start.row, me->saved_sizes);
		colrow_state_list_destroy (me->saved_sizes);
		me->saved_sizes = NULL;
	} else {
		me->saved_sizes = colrow_get_states (me->dst.sheet,
			FALSE, me->dst.range.start.row, me->dst.range.end.row);
		rows_height_update (me->dst.sheet, &me->dst.range, FALSE);
	}

	me->content = content;
	me->has_been_through_cycle = TRUE;

	/* Make the newly pasted content the selection (this queues a redraw) */
	sv = sheet_get_view (me->dst.sheet, wb_control_view (wbc));
	sv_selection_reset (sv);
	sv_selection_add_range (sv,
		me->dst.range.start.col, me->dst.range.start.row,
		me->dst.range.start.col, me->dst.range.start.row,
		me->dst.range.end.col, me->dst.range.end.row);
	sv_make_cell_visible (sv,
		me->dst.range.start.col, me->dst.range.start.row, FALSE);

	return FALSE;
}

static gboolean
cmd_paste_copy_undo (GnmCommand *cmd, WorkbookControl *wbc)
{
	return cmd_paste_copy_impl (cmd, wbc, TRUE);
}

static gboolean
cmd_paste_copy_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	return cmd_paste_copy_impl (cmd, wbc, FALSE);
}

static void
cmd_paste_copy_finalize (GObject *cmd)
{
	CmdPasteCopy *me = CMD_PASTE_COPY (cmd);

	if (me->saved_sizes)
		me->saved_sizes = colrow_state_list_destroy (me->saved_sizes);
	if (me->content) {
		if (me->has_been_through_cycle)
			cellregion_unref (me->content);
		me->content = NULL;
	}
	gnm_command_finalize (cmd);
}

gboolean
cmd_paste_copy (WorkbookControl *wbc,
		GnmPasteTarget const *pt, GnmCellRegion *cr)
{
	GObject *obj;
	CmdPasteCopy *me;

	g_return_val_if_fail (pt != NULL, TRUE);
	g_return_val_if_fail (IS_SHEET (pt->sheet), TRUE);

	obj = g_object_new (CMD_PASTE_COPY_TYPE, NULL);
	me = CMD_PASTE_COPY (obj);

	me->cmd.sheet = pt->sheet;
	me->cmd.size = 1;  /* FIXME?  */
	me->cmd.cmd_descriptor = g_strdup_printf (_("Pasting into %s"),
						     range_name (&pt->range));
	me->dst = *pt;
	me->content = cr;
	me->has_been_through_cycle = FALSE;
	me->saved_sizes = NULL;

	/* If the input is only objects ignore all this range stuff */
	if (cr->cols < 1 || cr->rows < 1) {

	/* If the destination is a singleton paste the entire content */
	} else if (range_is_singleton (&me->dst.range)) {
		if (pt->paste_flags & PASTE_TRANSPOSE) {
			me->dst.range.end.col = me->dst.range.start.col + cr->rows -1;
			me->dst.range.end.row = me->dst.range.start.row + cr->cols -1;
		} else {
			me->dst.range.end.col = me->dst.range.start.col + cr->cols -1;
			me->dst.range.end.row = me->dst.range.start.row + cr->rows -1;
		}
	} else if (pt->paste_flags & PASTE_TRANSPOSE) {
		/* when transposed single rows or cols get replicated as needed */
		if (cr->cols == 1 && me->dst.range.start.col == me->dst.range.end.col) {
			me->dst.range.end.col = me->dst.range.start.col + cr->rows -1;
		} else if (cr->rows == 1 && me->dst.range.start.row == me->dst.range.end.row) {
			me->dst.range.end.row = me->dst.range.start.row + cr->cols -1;
		}
	} else if  (cr->cols != 1 || cr->rows != 1) {
		/* Note: when the source is a single cell, a single target merge is special */
		/* see clipboard.c (clipboard_paste_region)                                 */
		GnmRange const *merge = sheet_merge_is_corner (pt->sheet, &me->dst.range.start);
		if (merge != NULL && range_equal (&me->dst.range, merge)) {
			/* destination is a single merge */
			/* enlarge it such that the source fits */
			if (pt->paste_flags & PASTE_TRANSPOSE) {
				if ((me->dst.range.end.col - me->dst.range.start.col + 1) <
				    cr->rows)
					me->dst.range.end.col =
						me->dst.range.start.col + cr->rows -1;
				if ((me->dst.range.end.row - me->dst.range.start.row + 1) <
				    cr->cols)
					me->dst.range.end.row =
						me->dst.range.start.row + cr->cols -1;
			} else {
				if ((me->dst.range.end.col - me->dst.range.start.col + 1) <
				    cr->cols)
					me->dst.range.end.col =
						me->dst.range.start.col + cr->cols -1;
				if ((me->dst.range.end.row - me->dst.range.start.row + 1) <
				    cr->rows)
					me->dst.range.end.row =
						me->dst.range.start.row + cr->rows -1;
			}
		}
	}

	/* Use translate to do a quiet sanity check */
	if (range_translate (&me->dst.range, 0, 0)) {
		gnm_cmd_context_error_invalid (GNM_CMD_CONTEXT (wbc),
					me->cmd.cmd_descriptor,
					_("is beyond sheet boundaries"));
		g_object_unref (G_OBJECT (me));
		return TRUE;
	}

	/* no need to test if all we have are objects */
	if (cr->cols > 0 && cr->rows > 0 &&
	    sheet_range_splits_region (pt->sheet, &me->dst.range,
				       NULL, GNM_CMD_CONTEXT (wbc), me->cmd.cmd_descriptor)) {
		g_object_unref (G_OBJECT (me));
		return TRUE;
	}

	return command_push_undo (wbc, obj);
}

/******************************************************************/

#define CMD_AUTOFILL_TYPE        (cmd_autofill_get_type ())
#define CMD_AUTOFILL(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_AUTOFILL_TYPE, CmdAutofill))

typedef struct {
	GnmCommand cmd;

	GnmCellRegion *content;
	GnmPasteTarget dst;
	int base_col, base_row, w, h, end_col, end_row;
	gboolean default_increment;
	gboolean inverse_autofill;
} CmdAutofill;

static void
cmd_autofill_repeat (GnmCommand const *cmd, WorkbookControl *wbc)
{
	CmdAutofill const *orig = (CmdAutofill const *) cmd;
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	GnmRange const	*r = selection_first_range (sv,
		GNM_CMD_CONTEXT (wbc), _("Autofill"));

	if (r == NULL)
		return;

	cmd_autofill (wbc, sv_sheet (sv), orig->default_increment,
	      r->start.col, r->start.row, range_width (r), range_height (r),
	      r->start.col + (orig->end_col - orig->base_col),
	      r->start.row + (orig->end_row - orig->base_row),
	      orig->inverse_autofill);
}
MAKE_GNM_COMMAND (CmdAutofill, cmd_autofill, cmd_autofill_repeat);

static gboolean
cmd_autofill_undo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdAutofill *me = CMD_AUTOFILL (cmd);
	gboolean res;
	SheetView *sv;

	g_return_val_if_fail (wbc != NULL, TRUE);
	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->content != NULL, TRUE);

	res = clipboard_paste_region (me->content, &me->dst, GNM_CMD_CONTEXT (wbc));
	cellregion_unref (me->content);
	me->content = NULL;

	if (res)
		return TRUE;

	/* Make the newly pasted content the selection (this queues a redraw) */
	sv = sheet_get_view (me->dst.sheet, wb_control_view (wbc));
	sv_selection_reset (sv);
	sv_selection_add_range (sv,
		me->base_col, me->base_row,
		me->base_col, me->base_row,
		me->base_col + me->w-1,
		me->base_row + me->h-1);
	sv_make_cell_visible (sv, me->base_col, me->base_row, FALSE);

	return FALSE;
}

static gboolean
cmd_autofill_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdAutofill *me = CMD_AUTOFILL (cmd);
	SheetView *sv;

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->content == NULL, TRUE);

	me->content = clipboard_copy_range (me->dst.sheet, &me->dst.range);

	g_return_val_if_fail (me->content != NULL, TRUE);

	/* FIXME : when we split autofill to support hints and better validation
	 * move this in there.
	 */
	sheet_clear_region (me->dst.sheet,
		me->dst.range.start.col, me->dst.range.start.row,
		me->dst.range.end.col,   me->dst.range.end.row,
		CLEAR_VALUES | CLEAR_MERGES | CLEAR_NOCHECKARRAY | CLEAR_RECALC_DEPS,
		GNM_CMD_CONTEXT (wbc));

	if (me->cmd.size == 1)
		me->cmd.size += (g_slist_length (me->content->content) +
				    g_slist_length (me->content->styles) +
				    1);
	if (me->inverse_autofill)
		sheet_autofill (me->dst.sheet, me->default_increment,
			me->end_col, me->end_row, me->w, me->h,
			me->base_col, me->base_row);
	else
		sheet_autofill (me->dst.sheet, me->default_increment,
			me->base_col, me->base_row, me->w, me->h,
			me->end_col, me->end_row);

	/* Make the newly filled content the selection (this queues a redraw) */
	sv = sheet_get_view (me->dst.sheet, wb_control_view (wbc));
	sv_selection_reset (sv);
	sv_selection_add_range (sv,
		me->base_col, me->base_row,
		me->base_col, me->base_row,
		me->end_col, me->end_row);

	sheet_region_queue_recalc (me->dst.sheet, &me->dst.range);
	sheet_range_calc_spans (me->dst.sheet, &me->dst.range, SPANCALC_RENDER);
	sheet_flag_status_update_range (me->dst.sheet, &me->dst.range);
	sv_make_cell_visible (sv, me->base_col, me->base_row, FALSE);

	return FALSE;
}

static void
cmd_autofill_finalize (GObject *cmd)
{
	CmdAutofill *me = CMD_AUTOFILL (cmd);

	if (me->content) {
		cellregion_unref (me->content);
		me->content = NULL;
	}
	gnm_command_finalize (cmd);
}

gboolean
cmd_autofill (WorkbookControl *wbc, Sheet *sheet,
	      gboolean default_increment,
	      int base_col, int base_row,
	      int w, int h, int end_col, int end_row,
	      gboolean inverse_autofill)
{
	GObject *obj;
	CmdAutofill *me;
	GnmRange target, src;

	g_return_val_if_fail (IS_SHEET (sheet), TRUE);

	/* This would be meaningless */
	if (base_col+w-1 == end_col && base_row+h-1 == end_row)
		return FALSE;

	if (inverse_autofill) {
		if (end_col != base_col + w - 1) {
			range_init (&target, base_col, base_row,
				    end_col - w, end_row);
			range_init (&src, end_col - w + 1, base_row,
				    end_col, end_row);
		} else {
			range_init (&target, base_col, base_row,
				    end_col, end_row - h);
			range_init (&src, base_col, end_row - h + 1,
				    end_col, end_row);
		}
	} else {
		if (end_col != base_col + w - 1) {
			range_init (&target, base_col + w, base_row,
				    end_col, end_row);
			range_init (&src, base_col, base_row,
				    base_col + w - 1, end_row);
		} else {
			range_init (&target, base_col, base_row + h,
				    end_col, end_row);
			range_init (&src, base_col, base_row,
				    end_col, base_row + h - 1);
		}
	}

	/* We don't support clearing regions, when a user uses the autofill
	 * cursor to 'shrink' a selection
	 */
	if (target.start.col > target.end.col || target.start.row > target.end.row)
		return TRUE;

	/* Check arrays or merged regions in src or target regions */
	if (sheet_range_splits_region (sheet, &target, NULL, GNM_CMD_CONTEXT (wbc), _("Autofill")) ||
	    sheet_range_splits_region (sheet, &src, NULL, GNM_CMD_CONTEXT (wbc), _("Autofill")))
		return TRUE;

	obj = g_object_new (CMD_AUTOFILL_TYPE, NULL);
	me = CMD_AUTOFILL (obj);

	me->content = NULL;
	me->dst.sheet = sheet;
	me->dst.paste_flags = PASTE_CONTENT | PASTE_FORMATS;
	me->dst.range = target;

	me->base_col = base_col;
	me->base_row = base_row,
	me->w = w;
	me->h = h;
	me->end_col = end_col;
	me->end_row = end_row;
	me->default_increment = default_increment;
	me->inverse_autofill = inverse_autofill;

	me->cmd.sheet = sheet;
	me->cmd.size = 1;  /* Changed in initial redo.  */
	me->cmd.cmd_descriptor = g_strdup_printf (_("Autofilling %s"),
		range_name (&me->dst.range));

	return command_push_undo (wbc, obj);
}

/******************************************************************/

#define CMD_COPYREL_TYPE        (cmd_copyrel_get_type ())
#define CMD_COPYREL(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_COPYREL_TYPE, CmdCopyRel))

typedef struct {
	GnmCommand cmd;

	GnmCellRegion *content;
	GnmPasteTarget dst, src;
	int dx, dy;
	const char *name;
} CmdCopyRel;

static void
cmd_copyrel_repeat (GnmCommand const *cmd, WorkbookControl *wbc)
{
	CmdCopyRel const *orig = (CmdCopyRel const *) cmd;
	cmd_copyrel (wbc, orig->dx, orig->dy, orig->name);
}
MAKE_GNM_COMMAND (CmdCopyRel, cmd_copyrel, cmd_copyrel_repeat);

static gboolean
cmd_copyrel_undo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdCopyRel *me = CMD_COPYREL (cmd);
	gboolean res;
	SheetView *sv;

	g_return_val_if_fail (wbc != NULL, TRUE);
	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->content != NULL, TRUE);

	res = clipboard_paste_region (me->content, &me->dst, GNM_CMD_CONTEXT (wbc));
	cellregion_unref (me->content);
	me->content = NULL;

	if (res)
		return TRUE;

	/* Make the newly pasted content the selection (this queues a redraw) */
	sv = sheet_get_view (me->dst.sheet, wb_control_view (wbc));
	sv_selection_reset (sv);
	sv_selection_add_range (sv,
				me->dst.range.start.col, me->dst.range.start.row,
				me->dst.range.start.col, me->dst.range.start.row,
				me->dst.range.end.col, me->dst.range.end.row);
	sv_make_cell_visible (sv,
			      me->dst.range.start.col, me->dst.range.start.row,
			      FALSE);

	return FALSE;
}

static gboolean
cmd_copyrel_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdCopyRel *me = CMD_COPYREL (cmd);
	SheetView *sv;
	GnmCellRegion *content;
	gboolean res;

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->content == NULL, TRUE);

	me->content = clipboard_copy_range (me->dst.sheet, &me->dst.range);

	g_return_val_if_fail (me->content != NULL, TRUE);

	sheet_clear_region (me->dst.sheet,
		me->dst.range.start.col, me->dst.range.start.row,
		me->dst.range.end.col,   me->dst.range.end.row,
		CLEAR_VALUES | CLEAR_MERGES | CLEAR_NOCHECKARRAY | CLEAR_RECALC_DEPS,
		GNM_CMD_CONTEXT (wbc));

	content = clipboard_copy_range (me->src.sheet, &me->src.range);
	res = clipboard_paste_region (content, &me->dst, GNM_CMD_CONTEXT (wbc));
	cellregion_unref (content);
	if (res)
		return TRUE;

	/* Make the newly filled content the selection (this queues a redraw) */
	sv = sheet_get_view (me->dst.sheet, wb_control_view (wbc));
	sv_selection_reset (sv);
	sv_selection_add_range (sv,
				me->dst.range.start.col, me->dst.range.start.row,
				me->dst.range.start.col, me->dst.range.start.row,
				me->dst.range.end.col, me->dst.range.end.row);

	sheet_region_queue_recalc (me->dst.sheet, &me->dst.range);
	sheet_range_calc_spans (me->dst.sheet, &me->dst.range, SPANCALC_RENDER);
	sheet_flag_status_update_range (me->dst.sheet, &me->dst.range);
	sv_make_cell_visible (sv,
			      me->dst.range.start.col, me->dst.range.start.row,
			      FALSE);

	return FALSE;
}

static void
cmd_copyrel_finalize (GObject *cmd)
{
	CmdCopyRel *me = CMD_COPYREL (cmd);

	if (me->content) {
		cellregion_unref (me->content);
		me->content = NULL;
	}
	gnm_command_finalize (cmd);
}

gboolean
cmd_copyrel (WorkbookControl *wbc,
	     int dx, int dy,
	     const char *name)
{
	GObject *obj;
	CmdCopyRel *me;
	GnmRange target, src;
	SheetView *sv  = wb_control_cur_sheet_view (wbc);
	Sheet *sheet = sv->sheet;
	GnmCellPos const *pos = sv_is_singleton_selected (sv);

	if (!pos)
		return FALSE;

	target.start = target.end = *pos;

	src = target;
	src.start.col += dx;
	src.end.col += dx;
	src.start.row += dy;
	src.end.row += dy;
	if (src.start.col < 0 || src.start.col >= SHEET_MAX_COLS ||
	    src.start.row < 0 || src.start.row >= SHEET_MAX_ROWS)
		return FALSE;

	/* Check arrays or merged regions in src or target regions */
	if (sheet_range_splits_region (sheet, &target, NULL, GNM_CMD_CONTEXT (wbc), name) ||
	    sheet_range_splits_region (sheet, &src, NULL, GNM_CMD_CONTEXT (wbc), name))
		return TRUE;

	obj = g_object_new (CMD_COPYREL_TYPE, NULL);
	me = CMD_COPYREL (obj);

	me->content = NULL;
	me->dst.sheet = sheet;
	me->dst.paste_flags = PASTE_CONTENT | PASTE_FORMATS;
	me->dst.range = target;
	me->src.sheet = sheet;
	me->src.paste_flags = PASTE_CONTENT | PASTE_FORMATS;
	me->src.range = src;
	me->dx = dx;
	me->dy = dy;
	me->name = name;

	me->cmd.sheet = sheet;
	me->cmd.size = 1;
	me->cmd.cmd_descriptor = g_strdup (name);

	return command_push_undo (wbc, obj);
}

/******************************************************************/


#define CMD_AUTOFORMAT_TYPE        (cmd_autoformat_get_type ())
#define CMD_AUTOFORMAT(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_AUTOFORMAT_TYPE, CmdAutoFormat))

typedef struct {
	GnmCellPos pos;
	GnmStyleList *styles;
} CmdAutoFormatOldStyle;

typedef struct {
	GnmCommand cmd;

	GSList         *selection;   /* Selections on the sheet */
	GSList         *old_styles;  /* Older styles, one style_list per selection range*/

	FormatTemplate *ft;    /* Template that has been applied */
} CmdAutoFormat;

static void
cmd_autoformat_repeat (GnmCommand const *cmd, WorkbookControl *wbc)
{
	CmdAutoFormat const *orig = (CmdAutoFormat const *) cmd;
	cmd_selection_autoformat (wbc, format_template_clone (orig->ft));
}
MAKE_GNM_COMMAND (CmdAutoFormat, cmd_autoformat, cmd_autoformat_repeat);

static gboolean
cmd_autoformat_undo (GnmCommand *cmd,
		     G_GNUC_UNUSED WorkbookControl *wbc)
{
	CmdAutoFormat *me = CMD_AUTOFORMAT (cmd);

	g_return_val_if_fail (me != NULL, TRUE);

	if (me->old_styles) {
		GSList *l1 = me->old_styles;
		GSList *l2 = me->selection;

		for (; l1; l1 = l1->next, l2 = l2->next) {
			GnmRange *r;
			CmdAutoFormatOldStyle *os = l1->data;
			SpanCalcFlags flags = sheet_style_set_list (me->cmd.sheet,
					    &os->pos, FALSE, os->styles);

			g_return_val_if_fail (l2 && l2->data, TRUE);

			r = l2->data;
			sheet_range_calc_spans (me->cmd.sheet, r, flags);
			if (flags != SPANCALC_SIMPLE)
				rows_height_update (me->cmd.sheet, r, TRUE);
		}
	}

	return FALSE;
}

static gboolean
cmd_autoformat_redo (GnmCommand *cmd,
		     G_GNUC_UNUSED WorkbookControl *wbc)
{
	CmdAutoFormat *me = CMD_AUTOFORMAT (cmd);

	g_return_val_if_fail (me != NULL, TRUE);

	format_template_apply_to_sheet_regions (me->ft,
		me->cmd.sheet, me->selection);

	return FALSE;
}

static void
cmd_autoformat_finalize (GObject *cmd)
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

	range_fragment_free (me->selection);
	me->selection = NULL;

	format_template_free (me->ft);

	gnm_command_finalize (cmd);
}

/**
 * cmd_selection_autoformat:
 * @context: the context.
 * @ft: The format template that was applied
 *
 * Return value: TRUE if there was a problem
 **/
gboolean
cmd_selection_autoformat (WorkbookControl *wbc, FormatTemplate *ft)
{
	GObject *obj;
	CmdAutoFormat *me;
	GString   *names;
	GSList    *l;
	SheetView *sv = wb_control_cur_sheet_view (wbc);

	obj = g_object_new (CMD_AUTOFORMAT_TYPE, NULL);
	me = CMD_AUTOFORMAT (obj);

	me->selection = selection_get_ranges (sv, FALSE); /* Regions may overlap */
	me->ft  = ft;
	me->cmd.sheet = sv_sheet (sv);
	me->cmd.size = 1;  /* FIXME?  */

	if (!format_template_check_valid (ft, me->selection, GNM_CMD_CONTEXT (wbc))) {
		g_object_unref (G_OBJECT (me));
		return TRUE;
	}

	me->old_styles = NULL;
	for (l = me->selection; l; l = l->next) {
		CmdFormatOldStyle *os;
		GnmRange range = *((GnmRange const *) l->data);

		/* Store the containing range to handle borders */
		if (range.start.col > 0) range.start.col--;
		if (range.start.row > 0) range.start.row--;
		if (range.end.col < SHEET_MAX_COLS-1) range.end.col++;
		if (range.end.row < SHEET_MAX_ROWS-1) range.end.row++;

		os = g_new (CmdFormatOldStyle, 1);

		os->styles = sheet_style_get_list (me->cmd.sheet, &range);
		os->pos = range.start;

		me->old_styles = g_slist_append (me->old_styles, os);
	}

	names = cmd_range_list_to_string_utility (me->cmd.sheet, me->selection);
	me->cmd.cmd_descriptor = g_strdup_printf (_("Autoformatting %s"),
						     names->str);
	g_string_free (names, TRUE);

	return command_push_undo (wbc, obj);
}

/******************************************************************/

#define CMD_UNMERGE_CELLS_TYPE        (cmd_unmerge_cells_get_type ())
#define CMD_UNMERGE_CELLS(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_UNMERGE_CELLS_TYPE, CmdUnmergeCells))

typedef struct {
	GnmCommand cmd;

	Sheet	*sheet;
	GArray	*unmerged_regions;
	GArray	*ranges;
} CmdUnmergeCells;

static void
cmd_unmerge_cells_repeat (GnmCommand const *cmd, WorkbookControl *wbc)
{
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	GSList *range_list = selection_get_ranges (sv, FALSE);
	cmd_unmerge_cells (wbc, sv_sheet (sv), range_list);
	range_fragment_free (range_list);
}
MAKE_GNM_COMMAND (CmdUnmergeCells, cmd_unmerge_cells, cmd_unmerge_cells_repeat);

static gboolean
cmd_unmerge_cells_undo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdUnmergeCells *me = CMD_UNMERGE_CELLS (cmd);
	unsigned i;

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->unmerged_regions != NULL, TRUE);

	for (i = 0 ; i < me->unmerged_regions->len ; ++i) {
		GnmRange const *tmp = &(g_array_index (me->unmerged_regions, GnmRange, i));
		sheet_redraw_range (me->cmd.sheet, tmp);
		sheet_merge_add (me->cmd.sheet, tmp, FALSE, GNM_CMD_CONTEXT (wbc));
		sheet_range_calc_spans (me->cmd.sheet, tmp, SPANCALC_RE_RENDER);
	}

	g_array_free (me->unmerged_regions, TRUE);
	me->unmerged_regions = NULL;

	return FALSE;
}

static gboolean
cmd_unmerge_cells_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdUnmergeCells *me = CMD_UNMERGE_CELLS (cmd);
	unsigned i;

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->unmerged_regions == NULL, TRUE);

	me->unmerged_regions = g_array_new (FALSE, FALSE, sizeof (GnmRange));
	for (i = 0 ; i < me->ranges->len ; ++i) {
		GSList *ptr, *merged = sheet_merge_get_overlap (me->cmd.sheet,
			&(g_array_index (me->ranges, GnmRange, i)));
		for (ptr = merged ; ptr != NULL ; ptr = ptr->next) {
			GnmRange const tmp = *(GnmRange *)(ptr->data);
			g_array_append_val (me->unmerged_regions, tmp);
			sheet_merge_remove (me->cmd.sheet, &tmp, GNM_CMD_CONTEXT (wbc));
			sheet_range_calc_spans (me->cmd.sheet, &tmp,
						SPANCALC_RE_RENDER);
		}
		g_slist_free (merged);
	}

	return FALSE;
}

static void
cmd_unmerge_cells_finalize (GObject *cmd)
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

	gnm_command_finalize (cmd);
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
	GObject *obj;
	CmdUnmergeCells *me;
	GString *names;

	g_return_val_if_fail (IS_SHEET (sheet), TRUE);

	obj = g_object_new (CMD_UNMERGE_CELLS_TYPE, NULL);
	me = CMD_UNMERGE_CELLS (obj);

	me->cmd.sheet = sheet;
	me->cmd.size = 1;

	names = cmd_range_list_to_string_utility (sheet, selection);
	me->cmd.cmd_descriptor = g_strdup_printf (_("Unmerging %s"), names->str);
	g_string_free (names, TRUE);

	me->unmerged_regions = NULL;
	me->ranges = g_array_new (FALSE, FALSE, sizeof (GnmRange));
	for ( ; selection != NULL ; selection = selection->next) {
		GSList *merged = sheet_merge_get_overlap (sheet, selection->data);
		if (merged != NULL) {
			g_array_append_val (me->ranges, *(GnmRange *)selection->data);
			g_slist_free (merged);
		}
	}

	if (me->ranges->len <= 0) {
		g_object_unref (G_OBJECT (me));
		return TRUE;
	}

	return command_push_undo (wbc, obj);
}

/******************************************************************/

#define CMD_MERGE_CELLS_TYPE        (cmd_merge_cells_get_type ())
#define CMD_MERGE_CELLS(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_MERGE_CELLS_TYPE, CmdMergeCells))

typedef struct {
	GnmCommand cmd;
	GArray	*ranges;
	GSList	*old_content;
	gboolean center;
} CmdMergeCells;

static void
cmd_merge_cells_repeat (GnmCommand const *cmd, WorkbookControl *wbc);

MAKE_GNM_COMMAND (CmdMergeCells, cmd_merge_cells, cmd_merge_cells_repeat);

static void
cmd_merge_cells_repeat (GnmCommand const *cmd, WorkbookControl *wbc)
{
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	GSList *range_list = selection_get_ranges (sv, FALSE);
	cmd_merge_cells (wbc, sv_sheet (sv), range_list,
		CMD_MERGE_CELLS (cmd)->center);
	range_fragment_free (range_list);
}

static gboolean
cmd_merge_cells_undo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdMergeCells *me = CMD_MERGE_CELLS (cmd);
	unsigned i, flags;

	g_return_val_if_fail (me != NULL, TRUE);

	for (i = 0 ; i < me->ranges->len ; ++i) {
		GnmRange const *r = &(g_array_index (me->ranges, GnmRange, i));
		sheet_merge_remove (me->cmd.sheet, r, GNM_CMD_CONTEXT (wbc));
	}

	flags = PASTE_CONTENT | PASTE_FORMATS | PASTE_IGNORE_COMMENTS;
	if (me->center)
		flags |= PASTE_FORMATS;
	for (i = 0 ; i < me->ranges->len ; ++i) {
		GnmRange const *r = &(g_array_index (me->ranges, GnmRange, i));
		GnmPasteTarget pt;
		GnmCellRegion * c;

		g_return_val_if_fail (me->old_content != NULL, TRUE);

		c = me->old_content->data;
		clipboard_paste_region (c,
			paste_target_init (&pt, me->cmd.sheet, r, flags),
			GNM_CMD_CONTEXT (wbc));
		cellregion_unref (c);
		me->old_content = g_slist_remove (me->old_content, c);
	}
	g_return_val_if_fail (me->old_content == NULL, TRUE);

	return FALSE;
}

static gboolean
cmd_merge_cells_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdMergeCells *me = CMD_MERGE_CELLS (cmd);
	GnmStyle *align_center = NULL;
	Sheet *sheet;
	unsigned i;

	g_return_val_if_fail (me != NULL, TRUE);

	if (me->center) {
		align_center = mstyle_new ();
		mstyle_set_align_h (align_center, HALIGN_CENTER);
	}
	sheet = me->cmd.sheet;
	for (i = 0 ; i < me->ranges->len ; ++i) {
		GnmRange const *r = &(g_array_index (me->ranges, GnmRange, i));
		GSList *ptr, *merged = sheet_merge_get_overlap (sheet, r);

		/* save content before removing contained merged regions */
		me->old_content = g_slist_prepend (me->old_content,
			clipboard_copy_range (sheet, r));
		for (ptr = merged ; ptr != NULL ; ptr = ptr->next)
			sheet_merge_remove (sheet, ptr->data, GNM_CMD_CONTEXT (wbc));
		g_slist_free (merged);

		sheet_merge_add (sheet, r, TRUE, GNM_CMD_CONTEXT (wbc));
		if (me->center)
			sheet_apply_style (me->cmd.sheet, r, align_center);
	}

	if (me->center)
		mstyle_unref (align_center);
	me->old_content = g_slist_reverse (me->old_content);
	return FALSE;
}

static void
cmd_merge_cells_finalize (GObject *cmd)
{
	CmdMergeCells *me = CMD_MERGE_CELLS (cmd);

	if (me->old_content != NULL) {
		GSList *l;
		for (l = me->old_content ; l != NULL ; l = g_slist_remove (l, l->data))
			cellregion_unref (l->data);
		me->old_content = NULL;
	}

	if (me->ranges != NULL) {
		g_array_free (me->ranges, TRUE);
		me->ranges = NULL;
	}

	gnm_command_finalize (cmd);
}

/**
 * cmd_merge_cells:
 * @context: the context.
 *
 * Return value: TRUE if there was a problem
 **/
gboolean
cmd_merge_cells (WorkbookControl *wbc, Sheet *sheet, GSList const *selection,
		 gboolean center)
{
	GObject *obj;
	CmdMergeCells *me;
	GString *names;

	g_return_val_if_fail (IS_SHEET (sheet), TRUE);

	obj = g_object_new (CMD_MERGE_CELLS_TYPE, NULL);
	me = CMD_MERGE_CELLS (obj);

	me->cmd.sheet = sheet;
	me->cmd.size = 1;

	names = cmd_range_list_to_string_utility (sheet, selection);
	me->cmd.cmd_descriptor =
		g_strdup_printf ((center ? _("Merge and Center %s") :_("Merging %s")), names->str);
	g_string_free (names, TRUE);

	me->center = center;
	me->ranges = g_array_new (FALSE, FALSE, sizeof (GnmRange));
	for ( ; selection != NULL ; selection = selection->next) {
		GnmRange const *exist;
		GnmRange const *r = selection->data;
		if (range_is_singleton (selection->data))
			continue;
		if (NULL != (exist = sheet_merge_is_corner (sheet, &r->start)) &&
		    range_equal (r, exist))
			continue;
		g_array_append_val (me->ranges, *(GnmRange *)selection->data);
	}

	if (me->ranges->len <= 0) {
		g_object_unref (G_OBJECT (me));
		return TRUE;
	}

	return command_push_undo (wbc, obj);
}

/******************************************************************/

#define CMD_SEARCH_REPLACE_TYPE		(cmd_search_replace_get_type())
#define CMD_SEARCH_REPLACE(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_SEARCH_REPLACE_TYPE, CmdSearchReplace))

typedef struct {
	GnmCommand cmd;
	GnmSearchReplace *sr;

	/*
	 * Undo/redo use this list of SearchReplaceItems to do their
	 * work.  Note, that it is possible for a cell to occur
	 * multiple times in the list.
	 */
	GList *cells;
} CmdSearchReplace;

MAKE_GNM_COMMAND (CmdSearchReplace, cmd_search_replace, NULL);

typedef enum { SRI_text, SRI_comment } SearchReplaceItemType;

typedef struct {
	GnmEvalPos pos;
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
cmd_search_replace_undo (GnmCommand *cmd,
			 G_GNUC_UNUSED WorkbookControl *wbc)
{
	CmdSearchReplace *me = CMD_SEARCH_REPLACE (cmd);
	GList *tmp;

	/* Undo does replacements backwards.  */
	for (tmp = g_list_last (me->cells); tmp; tmp = tmp->prev) {
		SearchReplaceItem *sri = tmp->data;
		switch (sri->old_type) {
		case SRI_text:
		{
			GnmCell *cell = sheet_cell_get (sri->pos.sheet,
						     sri->pos.eval.col,
						     sri->pos.eval.row);
			sheet_cell_set_text (cell, sri->old.text, NULL);
			break;
		}
		case SRI_comment:
		{
			GnmComment *comment =
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
cmd_search_replace_redo (GnmCommand *cmd,
			 G_GNUC_UNUSED WorkbookControl *wbc)
{
	CmdSearchReplace *me = CMD_SEARCH_REPLACE (cmd);
	GList *tmp;

	/* Redo does replacements forward.  */
	for (tmp = me->cells; tmp; tmp = tmp->next) {
		SearchReplaceItem *sri = tmp->data;
		switch (sri->new_type) {
		case SRI_text:
		{
			GnmCell *cell = sheet_cell_get (sri->pos.sheet,
						     sri->pos.eval.col,
						     sri->pos.eval.row);
			sheet_cell_set_text (cell, sri->new.text, NULL);
			break;
		}
		case SRI_comment:
		{
			GnmComment *comment =
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
cmd_search_replace_do_cell (CmdSearchReplace *me, GnmEvalPos *ep,
			    gboolean test_run)
{
	GnmSearchReplace *sr = me->sr;

	SearchReplaceCellResult cell_res;
	SearchReplaceCommentResult comment_res;

	if (search_replace_cell (sr, ep, TRUE, &cell_res)) {
		GnmExpr const *expr;
		GnmValue *val;
		gboolean err;
		GnmParsePos pp;

		parse_pos_init_evalpos (&pp, ep);
		parse_text_value_or_expr (&pp, cell_res.new_text, &val, &expr,
			mstyle_get_format (cell_get_mstyle (cell_res.cell)),
			workbook_date_conv (cell_res.cell->base.sheet->workbook));

		/*
		 * FIXME: this is a hack, but parse_text_value_or_expr
		 * does not have a better way of signaling an error.
		 */
		err = val && gnm_expr_char_start_p (cell_res.new_text);

		if (val) value_release (val);
		if (expr) gnm_expr_unref (expr);

		if (err) {
			if (test_run) {
				if (sr->query_func)
					sr->query_func (SRQ_fail,
							sr,
							cell_res.cell,
							cell_res.old_text,
							cell_res.new_text);
				g_free (cell_res.old_text);
				g_free (cell_res.new_text);
				return TRUE;
			} else {
				switch (sr->error_behaviour) {
				case SRE_error: {
					/* FIXME: should go via expression.  */
					GString *s = g_string_new ("=ERROR(");
					gnm_strescape (s, cell_res.new_text);
					g_string_append_c (s, ')');
					g_free (cell_res.new_text);
					cell_res.new_text = g_string_free (s, FALSE);
					err = FALSE;
					break;
				}
				case SRE_string: {
					GString *s = g_string_new (NULL);
					gnm_strescape (s, cell_res.new_text);
					cell_res.new_text = g_string_free (s, FALSE);
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
			gboolean doit = TRUE;
			if (sr->query && sr->query_func) {
				int res = sr->query_func (SRQ_query,
							  sr,
							  cell_res.cell,
							  cell_res.old_text,
							  cell_res.new_text);
				if (res == -1) {
					g_free (cell_res.old_text);
					g_free (cell_res.new_text);
					return TRUE;
				}
				doit = (res == 0);
			}

			if (doit) {
				SearchReplaceItem *sri = g_new (SearchReplaceItem, 1);

				sheet_cell_set_text (cell_res.cell, cell_res.new_text, NULL);

				sri->pos = *ep;
				sri->old_type = sri->new_type = SRI_text;
				sri->old.text = cell_res.old_text;
				sri->new.text = cell_res.new_text;
				me->cells = g_list_prepend (me->cells, sri);

				cell_res.old_text = cell_res.new_text = NULL;
			}
		}

		g_free (cell_res.new_text);
		g_free (cell_res.old_text);
	}

	if (!test_run && search_replace_comment (sr, ep, TRUE, &comment_res)) {
		gboolean doit = TRUE;

		if (sr->query && sr->query_func) {
			int res = sr->query_func (SRQ_querycommment,
						  sr,
						  ep->sheet,
						  &ep->eval,
						  comment_res.old_text,
						  comment_res.new_text);
			if (res == -1) {
				g_free (comment_res.new_text);
				return TRUE;
			}
			doit = (res == 0);
		}

		if (doit) {
			SearchReplaceItem *sri = g_new (SearchReplaceItem, 1);
			sri->pos = *ep;
			sri->old_type = sri->new_type = SRI_comment;
			sri->old.comment = g_strdup (comment_res.old_text);
			sri->new.comment = comment_res.new_text;
			me->cells = g_list_prepend (me->cells, sri);

			cell_comment_text_set (comment_res.comment, comment_res.new_text);
		} else
			g_free (comment_res.new_text);
	}

	return FALSE;
}


static gboolean
cmd_search_replace_do (CmdSearchReplace *me,
		       G_GNUC_UNUSED Workbook *wb, Sheet *sheet,
		       gboolean test_run)
{
	GnmSearchReplace *sr = me->sr;
	GPtrArray *cells;
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

	cells = search_collect_cells (sr, sheet);

	for (i = 0; i < cells->len; i++) {
		GnmEvalPos *ep = g_ptr_array_index (cells, i);

		if (cmd_search_replace_do_cell (me, ep, test_run)) {
			result = TRUE;
			break;
		}
	}

	search_collect_cells_free (cells);

	if (!test_run) {
		/* Cells were added in the wrong order.  Correct.  */
		me->cells = g_list_reverse (me->cells);

		cmd_search_replace_update_after_action (me);
	}

	return result;
}


static void
cmd_search_replace_finalize (GObject *cmd)
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

	gnm_command_finalize (cmd);
}

gboolean
cmd_search_replace (WorkbookControl *wbc, Sheet *sheet, GnmSearchReplace *sr)
{
	GObject *obj;
	CmdSearchReplace *me;
	Workbook *wb = wb_control_workbook (wbc);

	g_return_val_if_fail (sr != NULL, TRUE);

	obj = g_object_new (CMD_SEARCH_REPLACE_TYPE, NULL);
	me = CMD_SEARCH_REPLACE (obj);

	me->cells = NULL;
	me->sr = search_replace_copy (sr);

	me->cmd.sheet = NULL;
	me->cmd.size = 1;  /* Corrected below. */
	me->cmd.cmd_descriptor = g_strdup (_("Search and Replace"));

	if (cmd_search_replace_do (me, wb, sheet, TRUE)) {
		/* There was an error and nothing was done.  */
		g_object_unref (obj);
		return TRUE;
	}

	cmd_search_replace_do (me, wb, sheet, FALSE);
	me->cmd.size += g_list_length (me->cells);

	command_register_undo (wbc, obj);
	return FALSE;
}

/******************************************************************/

#define CMD_COLROW_STD_SIZE_TYPE        (cmd_colrow_std_size_get_type ())
#define CMD_COLROW_STD_SIZE(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_COLROW_STD_SIZE_TYPE, CmdColRowStdSize))

typedef struct {
	GnmCommand cmd;

	Sheet		*sheet;
	gboolean	 is_cols;
	double		 new_default;
	double           old_default;
} CmdColRowStdSize;

MAKE_GNM_COMMAND (CmdColRowStdSize, cmd_colrow_std_size, NULL);

static gboolean
cmd_colrow_std_size_undo (GnmCommand *cmd,
			  G_GNUC_UNUSED WorkbookControl *wbc)
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
cmd_colrow_std_size_redo (GnmCommand *cmd,
			  G_GNUC_UNUSED WorkbookControl *wbc)
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
cmd_colrow_std_size_finalize (GObject *cmd)
{
	gnm_command_finalize (cmd);
}

gboolean
cmd_colrow_std_size (WorkbookControl *wbc, Sheet *sheet,
		     gboolean is_cols, double new_default)
{
	GObject *obj;
	CmdColRowStdSize *me;

	g_return_val_if_fail (IS_SHEET (sheet), TRUE);

	obj = g_object_new (CMD_COLROW_STD_SIZE_TYPE, NULL);
	me = CMD_COLROW_STD_SIZE (obj);

	me->sheet = sheet;
	me->is_cols = is_cols;
	me->new_default = new_default;
	me->old_default = 0;

	me->cmd.sheet = sheet;
	me->cmd.size = 1;  /* Changed in initial redo.  */
	me->cmd.cmd_descriptor = is_cols
		? g_strdup_printf (_("Setting default width of columns to %.2fpts"), new_default)
		: g_strdup_printf (_("Setting default height of rows to %.2fpts"), new_default);

	return command_push_undo (wbc, obj);
}

/******************************************************************/

#define CMD_ZOOM_TYPE        (cmd_zoom_get_type ())
#define CMD_ZOOM(o)     (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_ZOOM_TYPE, CmdZoom))

typedef struct {
	GnmCommand cmd;

	GSList		*sheets;
	double		 new_factor;
	double          *old_factors;
} CmdZoom;

MAKE_GNM_COMMAND (CmdZoom, cmd_zoom, NULL);

static gboolean
cmd_zoom_undo (GnmCommand *cmd,
	       G_GNUC_UNUSED WorkbookControl *wbc)
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
cmd_zoom_redo (GnmCommand *cmd,
	       G_GNUC_UNUSED WorkbookControl *wbc)
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
cmd_zoom_finalize (GObject *cmd)
{
	CmdZoom *me = CMD_ZOOM (cmd);

	if (me->sheets)
		g_slist_free (me->sheets);
	if (me->old_factors)
		g_free (me->old_factors);

	gnm_command_finalize (cmd);
}

gboolean
cmd_zoom (WorkbookControl *wbc, GSList *sheets, double factor)
{
	GObject *obj;
	CmdZoom *me;
	GString *namelist;
	GSList *l;
	int i;
	guint max_width;

	g_return_val_if_fail (wbc != NULL, TRUE);
	g_return_val_if_fail (sheets != NULL, TRUE);

	obj = g_object_new (CMD_ZOOM_TYPE, NULL);
	me = CMD_ZOOM (obj);

	me->sheets = sheets;
	me->old_factors = g_new0 (double, g_slist_length (sheets));
	me->new_factor  = factor;

	/* Make a list of all sheets to zoom and save zoom factor for each */
	namelist = g_string_new (NULL);
	for (i = 0, l = me->sheets; l != NULL; l = l->next, i++) {
		Sheet *sheet = l->data;

		g_string_append (namelist, sheet->name_unquoted);
		me->old_factors[i] = sheet->last_zoom_factor_used;

		if (l->next)
			g_string_append (namelist, ", ");
	}

	/* Make sure the string doesn't get overly wide */
	max_width = max_descriptor_width ();
	if (strlen (namelist->str) > max_width) {
		g_string_truncate (namelist, max_width - 3);
		g_string_append (namelist, "...");
	}

	me->cmd.sheet = NULL;
	me->cmd.size = 1;
	me->cmd.cmd_descriptor =
		g_strdup_printf (_("Zoom %s to %.0f%%"), namelist->str, factor * 100);

	g_string_free (namelist, TRUE);

	return command_push_undo (wbc, obj);
}

/******************************************************************/

#define CMD_OBJECTS_DELETE_TYPE (cmd_objects_delete_get_type ())
#define CMD_OBJECTS_DELETE(o)   (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_OBJECTS_DELETE_TYPE, CmdObjectsDelete))

typedef struct {
	GnmCommand cmd;
	GSList *objects;
} CmdObjectsDelete;

MAKE_GNM_COMMAND (CmdObjectsDelete, cmd_objects_delete, NULL);

static gboolean
cmd_objects_delete_redo (GnmCommand *cmd,
			G_GNUC_UNUSED WorkbookControl *wbc)
{
	CmdObjectsDelete *me = CMD_OBJECTS_DELETE (cmd);
	g_slist_foreach (me->objects, (GFunc) sheet_object_clear_sheet, NULL);
	return FALSE;
}

static gboolean
cmd_objects_delete_undo (GnmCommand *cmd,
			G_GNUC_UNUSED WorkbookControl *wbc)
{
	CmdObjectsDelete *me = CMD_OBJECTS_DELETE (cmd);
	g_slist_foreach (me->objects,
		(GFunc) sheet_object_set_sheet, me->cmd.sheet);
	return FALSE;
}

static void
cmd_objects_delete_finalize (GObject *cmd)
{
	CmdObjectsDelete *me = CMD_OBJECTS_DELETE (cmd);
	g_slist_foreach (me->objects, (GFunc) g_object_unref, NULL);
	g_slist_free (me->objects);
	gnm_command_finalize (cmd);
}

/* Absorbs the list, adding references to the content */
gboolean
cmd_objects_delete (WorkbookControl *wbc, GSList *objects,
		    char const *name)
{
	GObject *object;
	CmdObjectsDelete *me;

	g_return_val_if_fail (IS_WORKBOOK_CONTROL (wbc), TRUE);
	g_return_val_if_fail (objects != NULL, TRUE);

	object = g_object_new (CMD_OBJECTS_DELETE_TYPE, NULL);
	me = CMD_OBJECTS_DELETE (object);

	me->objects = objects;
	g_slist_foreach (me->objects, (GFunc) g_object_ref, NULL);

	me->cmd.sheet = sheet_object_get_sheet (objects->data);
	me->cmd.size = 1;
	me->cmd.cmd_descriptor = g_strdup (name ? name : _("Delete Object"));

	return command_push_undo (wbc, object);
}

/******************************************************************/

#define CMD_OBJECTS_MOVE_TYPE (cmd_objects_move_get_type ())
#define CMD_OBJECTS_MOVE(o)   (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_OBJECTS_MOVE_TYPE, CmdObjectsMove))

typedef struct {
	GnmCommand cmd;
	GSList *objects;
	GSList *anchors;
	gboolean objects_created, first_time;
} CmdObjectsMove;

MAKE_GNM_COMMAND (CmdObjectsMove, cmd_objects_move, NULL);

static gboolean
cmd_objects_move_redo (GnmCommand *cmd,
		       G_GNUC_UNUSED WorkbookControl *wbc)
{
	CmdObjectsMove *me = CMD_OBJECTS_MOVE (cmd);
	SheetObjectAnchor tmp;
	GSList *obj = me->objects, *anch = me->anchors;

	for (; obj != NULL && anch != NULL ; obj = obj->next, anch = anch->next) {
		/* If these were newly created objects remove them on undo and
		 * re-insert on subsequent redos */
		if (me->objects_created && !me->first_time) {
			if (NULL != sheet_object_get_sheet (obj->data))
				sheet_object_clear_sheet (obj->data);
			else
				sheet_object_set_sheet (obj->data, cmd->sheet);
		}
		sheet_object_anchor_cpy	(&tmp, sheet_object_get_anchor (obj->data));
		sheet_object_set_anchor	(obj->data, anch->data);
		sheet_object_anchor_cpy	(anch->data, &tmp);
	}
	me->first_time = FALSE;

	return FALSE;
}

static gboolean
cmd_objects_move_undo (GnmCommand *cmd, WorkbookControl *wbc)
{
	return cmd_objects_move_redo (cmd, wbc);
}

static void
cmd_objects_move_finalize (GObject *cmd)
{
	CmdObjectsMove *me = CMD_OBJECTS_MOVE (cmd);
	g_slist_foreach (me->objects, (GFunc) g_object_unref, NULL);
	g_slist_free (me->objects);
	g_slist_foreach (me->anchors, (GFunc) g_free, NULL);
	g_slist_free (me->anchors);
	gnm_command_finalize (cmd);
}

gboolean
cmd_objects_move (WorkbookControl *wbc, GSList *objects, GSList *anchors,
		  gboolean objects_created, char const *name)
{
	GObject *object;
	CmdObjectsMove *me;

	g_return_val_if_fail (IS_WORKBOOK_CONTROL (wbc), TRUE);
	g_return_val_if_fail (NULL != objects, TRUE);
	g_return_val_if_fail (NULL != anchors, TRUE);
	g_return_val_if_fail (g_slist_length (objects) == g_slist_length (anchors), TRUE);

	/*
	 * There is no need to move the object around, because this has
	 * already happened.
	 */

	object = g_object_new (CMD_OBJECTS_MOVE_TYPE, NULL);
	me = CMD_OBJECTS_MOVE (object);

	me->first_time = TRUE;
	me->objects_created  = objects_created;
	me->objects = objects;
	g_slist_foreach (me->objects, (GFunc) g_object_ref, NULL);
	me->anchors = anchors;

	me->cmd.sheet = sheet_object_get_sheet (objects->data);
	me->cmd.size = 1;
	me->cmd.cmd_descriptor = g_strdup (name);

	return command_push_undo (wbc, object);
}

/******************************************************************/

#define CMD_OBJECT_FORMAT_TYPE (cmd_object_format_get_type ())
#define CMD_OBJECT_FORMAT(o)   (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_OBJECT_FORMAT_TYPE, CmdObjectFormat))

typedef struct {
	GnmCommand cmd;
	GObject	 *so, *style;
	gboolean  first_time;
} CmdObjectFormat;

MAKE_GNM_COMMAND (CmdObjectFormat, cmd_object_format, NULL);

static gboolean
cmd_object_format_redo (GnmCommand *cmd, G_GNUC_UNUSED WorkbookControl *wbc)
{
	CmdObjectFormat *me = CMD_OBJECT_FORMAT (cmd);
	if (me->first_time)
		me->first_time = FALSE;
	else {
		GObject *prev;
		g_object_get (me->so, "style",  &prev, NULL);
		g_object_set (me->so, "style",  me->style, NULL);
		g_object_unref (me->style);
		me->style = prev;
	}
	sheet_set_dirty (me->cmd.sheet, TRUE);
	return FALSE;
}

static gboolean
cmd_object_format_undo (GnmCommand *cmd, WorkbookControl *wbc)
{
	return cmd_object_format_redo (cmd, wbc);
}

static void
cmd_object_format_finalize (GObject *cmd)
{
	CmdObjectFormat *me = CMD_OBJECT_FORMAT (cmd);
	g_object_unref (me->style);
	g_object_unref (me->so);
	gnm_command_finalize (cmd);
}

/* Pass in the original style, we assume that the dialog was doing
 * instant apply. */
gboolean
cmd_object_format (WorkbookControl *wbc, SheetObject *so,
		   gpointer orig_style)
{
	GObject *object;
	CmdObjectFormat *me;

	g_return_val_if_fail (IS_WORKBOOK_CONTROL (wbc), TRUE);
	g_return_val_if_fail (IS_SHEET_OBJECT (so), TRUE);

	object = g_object_new (CMD_OBJECT_FORMAT_TYPE, NULL);
	me = CMD_OBJECT_FORMAT (object);

	me->so    = g_object_ref (G_OBJECT (so));
	me->style = g_object_ref (G_OBJECT (orig_style));
	me->first_time = TRUE;

	me->cmd.sheet = sheet_object_get_sheet (so);
	me->cmd.size = 1;
	me->cmd.cmd_descriptor = g_strdup (_("Format Object"));

	return command_push_undo (wbc, object);
}
/******************************************************************/

#define CMD_REORGANIZE_SHEETS_TYPE        (cmd_reorganize_sheets_get_type ())
#define CMD_REORGANIZE_SHEETS(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_REORGANIZE_SHEETS_TYPE, CmdReorganizeSheets))

typedef struct {
	GnmCommand cmd;

	Workbook *wb;
	GSList      *new_order;
	GSList      *old_order;
	GSList      *changed_names;
	GSList      *new_names;
	GSList      *old_names;
	GSList      *deleted_sheets;
	GSList      *deleted_sheets_data;
	GSList      *color_changed;
	GSList      *new_colors_fore;
	GSList      *new_colors_back;
	GSList      *old_colors_fore;
	GSList      *old_colors_back;
	GSList      *protection_changed;
	GSList      *new_locks;
	GSList      *old_locks;
	GSList      *visibility_changed;
	GSList      *new_visibility;
	GSList      *old_visibility;
	guint         new_sheet_start;
	guint         new_sheet_count;
} CmdReorganizeSheets;

MAKE_GNM_COMMAND (CmdReorganizeSheets, cmd_reorganize_sheets, NULL);

typedef struct {
	char *name;
	guint pos;
	GnmCellRegion *content;
	ColRowStateList         *col_info;
	ColRowStateList         *row_info;
	GList                   *sheet_objects;
} cmd_reorganize_sheets_delete_t;

static cmd_reorganize_sheets_delete_t *
cmd_reorganize_sheets_delete_get_this_sheet_info (Workbook *wb, guint pos)
{
	cmd_reorganize_sheets_delete_t *data;
	Sheet *sheet;
	GnmRange r;
	GList *l;

	g_return_val_if_fail (wb != NULL, NULL);
	g_return_val_if_fail (pos < (guint)workbook_sheet_count (wb), NULL);

	data = g_new0 (cmd_reorganize_sheets_delete_t, 1);
	sheet = workbook_sheet_by_index (wb, pos);
	data->name = g_strdup (sheet->name_unquoted);
	data->pos = pos;
	data->content = clipboard_copy_range (sheet, range_init_full_sheet (&r));
	data->col_info = colrow_get_states (sheet, TRUE, 0, SHEET_MAX_COLS - 1);
	data->row_info = colrow_get_states (sheet, FALSE, 0, SHEET_MAX_ROWS - 1);
	if (data->sheet_objects != NULL) {
		for (l = data->sheet_objects; l != NULL; l = l->next) {
			GObject *so = l->data;
			g_object_unref (so);
		}
		g_list_free (data->sheet_objects);
		data->sheet_objects = NULL;
	} 
	data->sheet_objects = NULL;
	for (l = sheet->sheet_objects; l != NULL; l = l->next) {
		data->sheet_objects 
			= g_list_prepend (data->sheet_objects,
					  l->data);
	}
	data->sheet_objects = g_list_reverse (data->sheet_objects);
	for (l = data->sheet_objects; l != NULL; l = l->next) {
		SheetObject *so = l->data;
		g_object_ref (G_OBJECT (so));
		sheet_object_clear_sheet (so);
	}
	return data;
}

static void
cmd_reorganize_sheets_delete_shift_list (GSList *list, guint pos)
{
	while (list) {
		guint val = GPOINTER_TO_UINT (list->data);
		if (val == pos)
			g_warning ("Deleted Sheet index appears "
				   "in data list!");
		if (val > pos)
			list->data = GUINT_TO_POINTER (val - 1);
		list = list->next;
	}
}

static gint 
cmd_reorganize_sheets_delete_cmp_f (gconstpointer a,
				    gconstpointer b)
{
	guint const a_val = GPOINTER_TO_INT (a);
	guint const b_val = GPOINTER_TO_INT (b);

	if (a_val > b_val)
		return -1;
	if (a_val < b_val)
		return 1;
	return 0;
}


static void
cmd_reorganize_sheets_delete_get_all_sheets_info (GnmCommand *cmd, 
						  WorkbookControl *wbc)
{
	CmdReorganizeSheets *me = CMD_REORGANIZE_SHEETS (cmd);
	GSList *list;

	/* First we need to sort the to be deleted sheets from biggest */
        /* to smallest */

	me->deleted_sheets = g_slist_sort (me->deleted_sheets,
			       cmd_reorganize_sheets_delete_cmp_f);

	/* Then we retrieve the data */
	for (list = me->deleted_sheets; list != NULL; list = list->next) {
		guint pos = GPOINTER_TO_UINT (list->data);
	
		me->deleted_sheets_data 
			= g_slist_prepend (me->deleted_sheets_data,
					   cmd_reorganize_sheets_delete_get_this_sheet_info 
					   (me->wb, pos));

	/* and shift the remaining lists */
		cmd_reorganize_sheets_delete_shift_list (me->protection_changed, 
							 pos);
		cmd_reorganize_sheets_delete_shift_list (me->color_changed, 
							 pos);
		cmd_reorganize_sheets_delete_shift_list (me->new_order, 
							 pos);
	}
	me->deleted_sheets_data = g_slist_reverse (me->deleted_sheets_data);
}

static void
cmd_reorganize_sheets_delete_recreate_sheet (WorkbookControl *wbc, Workbook *wb, 
					     cmd_reorganize_sheets_delete_t *sheet)
{
	Sheet *a_new_sheet;
	guint pos = sheet->pos;
	GList *l;

	a_new_sheet = sheet_new (wb, sheet->name);
	if (pos != 0)
		workbook_sheet_attach (wb, a_new_sheet, 
				       workbook_sheet_by_index (wb, pos - 1));
	else {
		/*FIXME: shouldn't there be a way to make */
                /*workbook_sheet_attach insert the new sheet */
                /*at the beginning?*/
		workbook_sheet_attach (wb, a_new_sheet, 
				       workbook_sheet_by_index (wb, 0));
		workbook_sheet_move (a_new_sheet, -1);
	}
	if (sheet->content) {
		GnmPasteTarget pt;
		GnmRange r;
		
		clipboard_paste_region (sheet->content,
					paste_target_init (&pt, a_new_sheet, 
							   range_init_full_sheet(&r), 
							   PASTE_ALL_TYPES), 
					GNM_CMD_CONTEXT (wbc));
	}
	if (sheet->col_info)
		colrow_set_states (a_new_sheet, TRUE, 0, sheet->col_info);
	if (sheet->row_info)
		colrow_set_states (a_new_sheet, FALSE, 0, sheet->row_info);
	if (sheet->sheet_objects != NULL) {
		for (l = sheet->sheet_objects; l != NULL; l = l->next) {
			SheetObject *so = l->data;
			sheet_object_set_sheet (so, a_new_sheet);
			g_object_unref (G_OBJECT (so));
		}
		g_list_free (sheet->sheet_objects);
		sheet->sheet_objects = NULL;
	} 
}

static void
cmd_reorganize_sheets_delete_free (cmd_reorganize_sheets_delete_t *sheet)
{
	GList *l;

	g_return_if_fail (sheet != NULL);
	cellregion_unref (sheet->content);
	g_free (sheet->name);
	if (sheet->col_info)
		sheet->col_info = colrow_state_list_destroy (sheet->col_info);
	if (sheet->row_info)
		sheet->row_info = colrow_state_list_destroy (sheet->row_info);
	if (sheet->sheet_objects != NULL) {
		for (l = sheet->sheet_objects; l != NULL; l = l->next) {
			GObject *so = l->data;
			g_object_unref (so);
		}
		g_list_free (sheet->sheet_objects);
		sheet->sheet_objects = NULL;
	} 
	g_free (sheet);
}

static gboolean
cmd_reorganize_sheets_undo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdReorganizeSheets *me = CMD_REORGANIZE_SHEETS (cmd);
	guint n;
	GSList *list;

	g_return_val_if_fail (me != NULL, TRUE);

	workbook_set_dirty (me->wb, TRUE);

        /* undo reordering */
	if (me->new_order) {
		if (me->old_order == NULL) {
			GSList *l = me->new_order;
			guint new_pos = 0;
			me->old_order = g_slist_copy (l);
			while (l) {
				guint old_pos = GPOINTER_TO_UINT (l->data);
				g_slist_nth (me->old_order, old_pos)->data =
					GUINT_TO_POINTER (new_pos);
				l = l->next;
				new_pos++;
			}
		}
		if (workbook_sheet_reorder_by_idx (me->wb, me->old_order))
			return TRUE;
	}

	/* undo changing protection */
	if (me->protection_changed) {
		if (workbook_sheet_change_protection  
		    (me->wb, me->protection_changed, me->old_locks)) 
			return TRUE;
	}
	/* undo changing visibility */
	if (me->visibility_changed) {
		if (workbook_sheet_change_visibility  
		    (me->wb, me->visibility_changed, me->old_visibility)) 
			return TRUE;
	}

	/* undo changing colors */
	if (me->color_changed) {
		if (workbook_sheet_recolor  (me->wb, me->color_changed,
					     me->old_colors_fore, 
					     me->old_colors_back))
			return TRUE;
	}

	/* undo adding sheets */
	for (n = 0; n < me->new_sheet_count; n++) {
		Sheet *sheet = workbook_sheet_by_index (me->wb, me->new_sheet_start);
		if (sheet)
			command_undo_sheet_delete (sheet);
	}

	/* undo renaming sheets */
	if (workbook_sheet_rename (me->wb, me->changed_names, me->old_names, 
				   GNM_CMD_CONTEXT (wbc)))
		return TRUE;

	/* undo deleting sheets */
	for (list = me->deleted_sheets_data; list != NULL; list = list->next)
	{
		cmd_reorganize_sheets_delete_recreate_sheet 
			(wbc, me->wb, 
			 (cmd_reorganize_sheets_delete_t *)list->data);
	}

	return FALSE;
}

static gboolean
cmd_reorganize_sheets_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdReorganizeSheets *me = CMD_REORGANIZE_SHEETS (cmd);
	GSList *list, *names;

	g_return_val_if_fail (me != NULL, TRUE);

	workbook_set_dirty (me->wb, TRUE);

	/* deleting sheets */
	if (me->deleted_sheets != NULL && me->deleted_sheets_data == NULL)
		cmd_reorganize_sheets_delete_get_all_sheets_info (cmd, wbc);
	for (list = me->deleted_sheets; list != NULL; list = list->next) {
		guint pos = GPOINTER_TO_UINT (list->data);
		Sheet *sheet = workbook_sheet_by_index (me->wb, pos);
		command_redo_sheet_delete (sheet); 
	}

	/* renaming sheets */
	if (workbook_sheet_rename (me->wb, me->changed_names, me->new_names, 
				   GNM_CMD_CONTEXT (wbc)))
		return TRUE;

	/* adding sheets */
	list = me->changed_names;
	names = me->new_names;
	while (list && names) {
		if (GPOINTER_TO_INT (list->data) == -1) {
			char *name = names->data;
			Sheet *a_new_sheet ;
			gboolean free_name = (name == NULL);
			
			if (free_name)
				name = workbook_sheet_get_free_name
					(me->wb, _("Sheet"), TRUE, FALSE);
			a_new_sheet = sheet_new (me->wb, name);
			if (free_name)
				g_free (name);
			workbook_sheet_attach (me->wb, a_new_sheet, NULL);
		}
		list = list->next;
		names = names->next;
	} 

	/* changing colors */
	if (me->color_changed) {
		if (workbook_sheet_recolor  (me->wb, me->color_changed,
					     me->new_colors_fore,
					     me->new_colors_back)) 
			return TRUE;
	}

	/* changing protection */
	if (me->protection_changed) {
		if (workbook_sheet_change_protection  
		    (me->wb, me->protection_changed, me->new_locks)) 
			return TRUE;
	}
	/* changing visibility */
	if (me->visibility_changed) {
		if (workbook_sheet_change_visibility  
		    (me->wb, me->visibility_changed, me->new_visibility)) 
			return TRUE;
	}

        /* reordering */
	if (me->new_order) {
		if (workbook_sheet_reorder_by_idx (me->wb, me->new_order))
			return TRUE;
	}

	return FALSE;
}


static void
cb_slist_gdk_color_free (gpointer data,
			 G_GNUC_UNUSED gpointer user_data)
{
	if (data)
		gdk_color_free (data);
}


static void
cmd_reorganize_sheets_finalize (GObject *cmd)
{
	CmdReorganizeSheets *me = CMD_REORGANIZE_SHEETS (cmd);
	GSList *list;

	g_slist_free (me->old_order);
	me->new_order = NULL;

	g_slist_free (me->new_order);
	me->new_order = NULL;

	g_slist_free (me->changed_names);
	me->changed_names = NULL;

	g_slist_foreach (me->old_names, (GFunc)g_free, NULL);
	g_slist_free (me->old_names);
	me->old_names = NULL;

	g_slist_foreach (me->new_names, (GFunc)g_free, NULL);
	g_slist_free (me->new_names);
	me->new_names = NULL;

	g_slist_free (me->color_changed);
	me->color_changed = NULL;

	g_slist_free (me->protection_changed);
	me->protection_changed = NULL;

	g_slist_free (me->new_locks);
	me->new_locks = NULL;

	g_slist_free (me->old_locks);
	me->old_locks = NULL;

	g_slist_free (me->visibility_changed);
	me->visibility_changed = NULL;

	g_slist_free (me->new_visibility);
	me->new_visibility = NULL;

	g_slist_free (me->old_visibility);
	me->old_visibility = NULL;

	g_slist_free (me->deleted_sheets);
	me->deleted_sheets = NULL;

	for (list = me->deleted_sheets_data; list != NULL; list = list->next) {
		cmd_reorganize_sheets_delete_free 
			((cmd_reorganize_sheets_delete_t *)list->data);
	}
	g_slist_free (me->deleted_sheets_data);
	me->deleted_sheets_data = NULL;

	g_slist_foreach (me->new_colors_fore, cb_slist_gdk_color_free, NULL);
	g_slist_free (me->new_colors_fore);
	me->new_colors_fore = NULL;

	g_slist_foreach (me->new_colors_back, cb_slist_gdk_color_free, NULL);
	g_slist_free (me->new_colors_back);
	me->new_colors_back = NULL;

	g_slist_foreach (me->old_colors_fore, cb_slist_gdk_color_free, NULL);
	g_slist_free (me->old_colors_fore);
	me->old_colors_fore = NULL;

	g_slist_foreach (me->old_colors_back, cb_slist_gdk_color_free, NULL);
	g_slist_free (me->old_colors_back);
	me->old_colors_back = NULL;

	gnm_command_finalize (cmd);
}

static GSList *
cmd_reorganize_sheets_fix_list (GSList *list, guint start, guint count, 
		       gboolean append)
{
	GSList *l = list;
	GSList *ret = list;

	while (l != NULL && count > 0) {
		if (GPOINTER_TO_INT (l->data) == -1) {
			l->data = GUINT_TO_POINTER (start);
			start++;
			count--;
		}
		l = l->next;
	}
	if (append) {
		while (count > 0) {
			ret = g_slist_append (ret, GUINT_TO_POINTER (start));
			start++;			
			count--;
		}
	}
	return ret;
}

gboolean
cmd_reorganize_sheets (WorkbookControl *wbc, GSList *new_order,
		       GSList *changed_names, GSList *new_names, GSList *deleted_sheets,
		       GSList *color_changed, GSList *new_colors_back,
		       GSList *new_colors_fore,
		       GSList *protection_changed, GSList *new_locks,
		       GSList *visibility_changed, GSList *new_visibility)
{
	GObject *obj;
	CmdReorganizeSheets *me;
	Workbook *wb = wb_control_workbook (wbc);
	GSList *the_sheets;
	int selector = 0;

	obj = g_object_new (CMD_REORGANIZE_SHEETS_TYPE, NULL);
	me = CMD_REORGANIZE_SHEETS (obj);

	/* Store the specs for the object */
	me->wb = wb;
	me->new_order = new_order;
	me->old_order = NULL;
	me->changed_names = changed_names;
	me->new_names = new_names;
	me->old_names = NULL;
	me->color_changed = color_changed;
	me->new_colors_fore = new_colors_fore;
	me->new_colors_back = new_colors_back;
	me->old_colors_fore = NULL;
	me->old_colors_back = NULL;
	me->protection_changed = protection_changed;
	me->new_locks = new_locks;
	me->old_locks = NULL;
	me->visibility_changed = visibility_changed;
	me->new_visibility = new_visibility;
	me->old_visibility = NULL;
	me->deleted_sheets = deleted_sheets;
	me->deleted_sheets_data = NULL;
	me->new_sheet_start = workbook_sheet_count (wb);
	me->new_sheet_count = 0;

	the_sheets = changed_names;
	while (the_sheets) {
		if (-1 == GPOINTER_TO_INT (the_sheets->data)) {
			me->old_names = g_slist_prepend (me->old_names, NULL);
			me->new_sheet_count++;
		} else {
			Sheet *sheet = workbook_sheet_by_index 
				(wb, GPOINTER_TO_INT (the_sheets->data));
			me->old_names = g_slist_prepend
				(me->old_names, g_strdup (sheet->name_unquoted));
		}
		the_sheets = the_sheets->next;
	}
	me->old_names = g_slist_reverse (me->old_names);

	the_sheets = color_changed;
	while (the_sheets) {
		Sheet *sheet = workbook_sheet_by_index (wb, GPOINTER_TO_INT (the_sheets->data));
		if (sheet  == NULL) {
			me->old_colors_fore = g_slist_prepend (me->old_colors_fore, NULL);
			me->old_colors_back = g_slist_prepend (me->old_colors_back, NULL);
		} else {
			me->old_colors_fore = g_slist_prepend (me->old_colors_fore,
				   sheet->tab_text_color ?
				   gdk_color_copy (&sheet->tab_text_color->color) : NULL);
			me->old_colors_back = g_slist_prepend (me->old_colors_back,
				   sheet->tab_color ?
				   gdk_color_copy (&sheet->tab_color->color) : NULL);
		}
		the_sheets = the_sheets->next;
	}

	me->old_colors_fore = g_slist_reverse (me->old_colors_fore);
	me->old_colors_back = g_slist_reverse (me->old_colors_back);

	the_sheets = protection_changed;
	while (the_sheets) {
		Sheet *sheet = workbook_sheet_by_index (wb, GPOINTER_TO_INT (the_sheets->data));
		me->old_locks = g_slist_prepend (me->old_locks, GINT_TO_POINTER (
							 (sheet  != NULL) && sheet->is_protected));
		the_sheets = the_sheets->next;
	}
	me->old_locks = g_slist_reverse (me->old_locks);

	the_sheets = visibility_changed;
	while (the_sheets) {
		Sheet *sheet = workbook_sheet_by_index (wb, GPOINTER_TO_INT (the_sheets->data));
		me->old_visibility = g_slist_prepend (me->old_visibility, GINT_TO_POINTER (
							 (sheet == NULL) || sheet->is_visible));
		the_sheets = the_sheets->next;
	}
	me->old_visibility = g_slist_reverse (me->old_visibility);

	me->cmd.sheet = NULL;
	me->cmd.size = 1 + g_slist_length (color_changed) + g_slist_length (changed_names) + 1000 * g_slist_length (deleted_sheets);

	if (new_order != NULL)
		selector += (1 << 0);
	if (new_names != NULL && me->new_sheet_count == 0) {
		selector += (1 << 1);
	}
	if (new_names != NULL && me->new_sheet_count == g_slist_length (new_names)) {
		selector += (1 << 2);
	}
	if (new_names != NULL && me->new_sheet_count < g_slist_length (new_names) &&
		me->new_sheet_count > 0) {
		selector += (1 << 1) +(1 << 2) ;
	}
	if (color_changed != NULL)
		selector += (1 << 3);
	if (protection_changed != NULL)
		selector += (1 << 4);
	if (deleted_sheets != NULL) {
		selector += (1 << 5);
		if (deleted_sheets->next == NULL)
			selector += (1 << 6);
	}
	if (visibility_changed != NULL)
		selector += (1 << 7);


	switch (selector) {
	case 0:
		me->cmd.cmd_descriptor = g_strdup ("Nothing to do?");
		break;
	case (1 << 0):
		me->cmd.cmd_descriptor = g_strdup (_("Reordering Sheets"));
		break;
	case (1 << 1):
		me->cmd.cmd_descriptor = g_strdup (_("Renaming Sheets"));
		break;
	case (1 << 2):
		me->cmd.cmd_descriptor = g_strdup_printf (_("Appending %i Sheets"), 
							  me->new_sheet_count);
		break;
	case (1 << 3):
		me->cmd.cmd_descriptor = g_strdup (_("Changing Tab Colors"));
		break;
	case (1 << 4):
		me->cmd.cmd_descriptor = g_strdup (_("Changing Sheet Protection"));
		break;
	case (1 << 5):
		me->cmd.cmd_descriptor = g_strdup (_("Delete sheets"));
		break;
	case ((1 << 5) + (1 << 6)):
		me->cmd.cmd_descriptor = g_strdup (_("Delete a sheet"));
		break;
	case (1 << 7):
		me->cmd.cmd_descriptor = g_strdup (_("Changing Sheet Visibility"));
		break;
	default:
		me->cmd.cmd_descriptor = g_strdup (_("Reorganizing Sheets"));
		break;
	}

	
	if (!workbook_sheet_rename_check (me->wb, me->changed_names, 
					  me->new_names, 
					  me->deleted_sheets,
					 GNM_CMD_CONTEXT (wbc))) {
		g_object_unref (obj);
		return TRUE;
	}

	/* Replace the `-1' in most lists with the corresponding index value */
	me->new_order = cmd_reorganize_sheets_fix_list
		(me->new_order, me->new_sheet_start, me->new_sheet_count,
		 FALSE);
	me->color_changed = cmd_reorganize_sheets_fix_list
		(me->color_changed, me->new_sheet_start, me->new_sheet_count,
		 FALSE);
	me->protection_changed = cmd_reorganize_sheets_fix_list
		(me->protection_changed, me->new_sheet_start,
		 me->new_sheet_count, FALSE);

	/* Register the command object */
	return command_push_undo (wbc, obj);
}

/* Note:  cmd_rename_sheet does not free old_name or new_name */
/*        one of sheet and old_name may be NULL               */
gboolean
cmd_rename_sheet (WorkbookControl *wbc, Sheet *sheet,
		  char const *old_name, char const *new_name)
{

	Workbook *wb = wb_control_workbook (wbc);
	GSList *changed_names = NULL;
	GSList *new_names = NULL;

	g_return_val_if_fail (new_name != NULL, TRUE);
	g_return_val_if_fail (sheet != NULL || old_name != NULL, TRUE);

	if (sheet == NULL)
		sheet = workbook_sheet_by_name (wb, old_name);

	g_return_val_if_fail (sheet != NULL, TRUE);

	changed_names = g_slist_prepend (changed_names, 
					 GINT_TO_POINTER (sheet->index_in_wb));
	new_names = g_slist_prepend (new_names, g_strdup (new_name));

	return cmd_reorganize_sheets (wbc, NULL, changed_names, new_names,
				      NULL, NULL, NULL, NULL, NULL, NULL, 
				      NULL, NULL);
}

/******************************************************************/

#define CMD_SET_COMMENT_TYPE        (cmd_set_comment_get_type ())
#define CMD_SET_COMMENT(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_SET_COMMENT_TYPE, CmdSetComment))

typedef struct {
	GnmCommand cmd;

	Sheet           *sheet;
	GnmCellPos	        pos;
	gchar		*new_text;
	gchar		*old_text;
} CmdSetComment;

MAKE_GNM_COMMAND (CmdSetComment, cmd_set_comment, NULL);

static gboolean
cmd_set_comment_apply (Sheet *sheet, GnmCellPos *pos, char const *text)
{
	GnmComment   *comment;

	comment = cell_has_comment_pos (sheet, pos);
	if (comment) {
		if (text)
			cell_comment_text_set (comment, text);
		else {
			GnmRange r;
			r.start = *pos;
			r.end   = *pos;
			sheet_objects_clear (sheet, &r, CELL_COMMENT_TYPE);
		}
	} else if (text && (strlen (text) > 0))
		cell_set_comment (sheet, pos, NULL, text);

	sheet_set_dirty (sheet, TRUE);
	return FALSE;
}

static gboolean
cmd_set_comment_undo (GnmCommand *cmd,
		      G_GNUC_UNUSED WorkbookControl *wbc)
{
	CmdSetComment *me = CMD_SET_COMMENT (cmd);

	return cmd_set_comment_apply (me->sheet, &me->pos, me->old_text);
}

static gboolean
cmd_set_comment_redo (GnmCommand *cmd,
		      G_GNUC_UNUSED WorkbookControl *wbc)
{
	CmdSetComment *me = CMD_SET_COMMENT (cmd);

	return cmd_set_comment_apply (me->sheet, &me->pos, me->new_text);
}

static void
cmd_set_comment_finalize (GObject *cmd)
{
	CmdSetComment *me = CMD_SET_COMMENT (cmd);

	g_free (me->new_text);
	me->new_text = NULL;

	g_free (me->old_text);
	me->old_text = NULL;

	gnm_command_finalize (cmd);
}

gboolean
cmd_set_comment (WorkbookControl *wbc,
	      Sheet *sheet, GnmCellPos const *pos,
	      char const *new_text)
{
	GObject       *obj;
	CmdSetComment *me;
	GnmComment   *comment;
	char *where;

	g_return_val_if_fail (IS_SHEET (sheet), TRUE);
	g_return_val_if_fail (new_text != NULL, TRUE);

	obj = g_object_new (CMD_SET_COMMENT_TYPE, NULL);
	me = CMD_SET_COMMENT (obj);

	me->cmd.sheet = sheet;
	me->cmd.size = 1;
	if (strlen (new_text) < 1)
		me->new_text = NULL;
	else
		me->new_text    = g_strdup (new_text);
	where = cmd_cell_pos_name_utility (sheet, pos);
	me->cmd.cmd_descriptor =
		g_strdup_printf (me->new_text == NULL ?
				 _("Clearing comment of %s") :
				 _("Setting comment of %s"),
				 where);
	g_free (where);
	me->old_text    = NULL;
	me->pos         = *pos;
	me->sheet       = sheet;
	comment = cell_has_comment_pos (sheet, pos);
	if (comment)
		me->old_text = g_strdup (cell_comment_text_get (comment));

	/* Register the command object */
	return command_push_undo (wbc, obj);
}

/******************************************************************/

#define CMD_ANALYSIS_TOOL_TYPE        (cmd_analysis_tool_get_type ())
#define CMD_ANALYSIS_TOOL(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_ANALYSIS_TOOL_TYPE, CmdAnalysis_Tool))

typedef struct {
	GnmCommand         cmd;

	data_analysis_output_t  *dao;
	gpointer                specs;
	gboolean                specs_owned;
	analysis_tool_engine    engine;
	data_analysis_output_type_t type;

	ColRowStateList         *col_info;
	ColRowStateList         *row_info;
	GnmRange                   old_range;
	GnmCellRegion              *old_content;
} CmdAnalysis_Tool;

MAKE_GNM_COMMAND (CmdAnalysis_Tool, cmd_analysis_tool, NULL);

static gboolean
cmd_analysis_tool_undo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdAnalysis_Tool *me = CMD_ANALYSIS_TOOL (cmd);
	GnmPasteTarget pt;

	g_return_val_if_fail (me != NULL, TRUE);

	switch (me->type) {
	case NewSheetOutput:
		if (!command_undo_sheet_delete (me->dao->sheet))
			return TRUE;
		me->dao->sheet = NULL;
		break;
	case NewWorkbookOutput:
		g_warning ("How did we get here?");
		return TRUE;
		break;
	case RangeOutput:
	default:
		sheet_clear_region (me->dao->sheet,
				    me->old_range.start.col, me->old_range.start.row,
				    me->old_range.end.col, me->old_range.end.row,
				    CLEAR_COMMENTS | CLEAR_FORMATS | CLEAR_NOCHECKARRAY |
				    CLEAR_RECALC_DEPS | CLEAR_VALUES | CLEAR_MERGES,
				    GNM_CMD_CONTEXT (wbc));
		clipboard_paste_region (me->old_content,
			paste_target_init (&pt, me->dao->sheet, &me->old_range, PASTE_ALL_TYPES),
			GNM_CMD_CONTEXT (wbc));
		cellregion_unref (me->old_content);
		me->old_content = NULL;
		if (me->col_info) {
			dao_set_colrow_state_list (me->dao, TRUE, me->col_info);
			me->col_info = colrow_state_list_destroy (me->col_info);
		}
		if (me->row_info) {
			dao_set_colrow_state_list (me->dao, FALSE, me->row_info);
			me->row_info = colrow_state_list_destroy (me->row_info);
		}
		workbook_recalc (me->dao->sheet->workbook);
		sheet_update (me->dao->sheet);
	}

	return FALSE;
}

static gboolean
cmd_analysis_tool_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	gpointer continuity = NULL;
	CmdAnalysis_Tool *me = CMD_ANALYSIS_TOOL (cmd);

	g_return_val_if_fail (me != NULL, TRUE);

	if (me->col_info)
		me->col_info = colrow_state_list_destroy (me->col_info);
	me->col_info = dao_get_colrow_state_list (me->dao, TRUE);
	if (me->row_info)
		me->row_info = colrow_state_list_destroy (me->row_info);
	me->row_info = dao_get_colrow_state_list (me->dao, FALSE);

	if (me->engine (me->dao, me->specs, TOOL_ENGINE_PREPARE_OUTPUT_RANGE, NULL)
	    || me->engine (me->dao, me->specs, TOOL_ENGINE_UPDATE_DESCRIPTOR,
			   &me->cmd.cmd_descriptor)
	    || cmd_dao_is_locked_effective (me->dao, wbc, me->cmd.cmd_descriptor)
	    || me->engine (me->dao, me->specs, TOOL_ENGINE_LAST_VALIDITY_CHECK, &continuity))
		return TRUE;

	switch (me->type) {
	case NewSheetOutput:
		me->old_content = NULL;
		break;
	case NewWorkbookOutput:
		/* No undo in this case (see below) */
		me->old_content = NULL;
		break;
	case RangeOutput:
	default:
		range_init (&me->old_range, me->dao->start_col, me->dao->start_row,
			    me->dao->start_col + me->dao->cols - 1,
			    me->dao->start_row + me->dao->rows - 1);
		me->old_content = clipboard_copy_range (me->dao->sheet, &me->old_range);
		break;
	}

	if (me->engine (me->dao, me->specs, TOOL_ENGINE_FORMAT_OUTPUT_RANGE, NULL))
		return TRUE;

	if (me->engine (me->dao, me->specs, TOOL_ENGINE_PERFORM_CALC, &continuity)) {
		if (me->type == RangeOutput) {
			g_warning ("This is too late for failure! The target region has "
				   "already been formatted!");
		} else
			return TRUE;
	}
	if (continuity) {
		g_warning ("There shouldn't be any data left in here!");
	}

	dao_autofit_columns (me->dao);
	sheet_set_dirty (me->dao->sheet, TRUE);
	workbook_recalc (me->dao->sheet->workbook);
	sheet_update (me->dao->sheet);

	/* The concept of an undo if we create a new worksheet is extremely strange,
	 * since we have separate undo/redo queues per worksheet.
	 * Users can simply delete the worksheet if they so desire.
	 */

	return (me->type == NewWorkbookOutput);
}

static void
cmd_analysis_tool_finalize (GObject *cmd)
{
	CmdAnalysis_Tool *me = CMD_ANALYSIS_TOOL (cmd);

	if (me->col_info)
		me->col_info = colrow_state_list_destroy (me->col_info);
	if (me->row_info)
		me->row_info = colrow_state_list_destroy (me->row_info);

	me->engine (me->dao, me->specs, TOOL_ENGINE_CLEAN_UP, NULL);

	if (me->specs_owned) {
		g_free (me->specs);
		g_free (me->dao);
	}
	if (me->old_content)
		cellregion_unref (me->old_content);

	gnm_command_finalize (cmd);
}

/*
 * Note: this takes ownership of specs and dao if and if only the command
 * succeeds.
 */
gboolean
cmd_analysis_tool (WorkbookControl *wbc, G_GNUC_UNUSED Sheet *sheet,
		   data_analysis_output_t *dao, gpointer specs,
		   analysis_tool_engine engine)
{
	GObject *obj;
	CmdAnalysis_Tool *me;
	gboolean trouble;

	g_return_val_if_fail (dao != NULL, TRUE);
	g_return_val_if_fail (specs != NULL, TRUE);
	g_return_val_if_fail (engine != NULL, TRUE);

	obj = g_object_new (CMD_ANALYSIS_TOOL_TYPE, NULL);
	me = CMD_ANALYSIS_TOOL (obj);

	dao->wbc = wbc;

	/* Store the specs for the object */
	me->specs = specs;
	me->specs_owned = FALSE;
	me->dao = dao;
	me->engine = engine;
	me->cmd.cmd_descriptor = NULL;
	if (me->engine (me->dao, me->specs, TOOL_ENGINE_UPDATE_DAO, NULL)) {
		g_object_unref (obj);
		return TRUE;
	}
	me->engine (me->dao, me->specs, TOOL_ENGINE_UPDATE_DESCRIPTOR, &me->cmd.cmd_descriptor);
	me->cmd.sheet = NULL;
	me->type = dao->type;
	me->row_info = NULL;
	me->col_info = NULL;

	/* We divide by 2 since many cells will be empty*/
	me->cmd.size = 1 + dao->rows * dao->cols / 2;

	/* Register the command object */
	trouble = command_push_undo (wbc, obj);

	if (!trouble)
		me->specs_owned = TRUE;

	return trouble;
}

/******************************************************************/

#define CMD_MERGE_DATA_TYPE        (cmd_merge_data_get_type ())
#define CMD_MERGE_DATA(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_MERGE_DATA_TYPE, CmdMergeData))

typedef struct {
	GnmCommand cmd;
	GnmValue *merge_zone;
	GSList *merge_fields;
	GSList *merge_data;
	GSList *sheet_list;
	Sheet *sheet;
	gint n;
} CmdMergeData;

MAKE_GNM_COMMAND (CmdMergeData, cmd_merge_data, NULL);

static void
cmd_merge_data_delete_sheets (gpointer data, gpointer success)
{
	Sheet *sheet = data;

	if (!command_undo_sheet_delete (sheet))
		*(gboolean *)success = FALSE;
}

static gboolean
cmd_merge_data_undo (GnmCommand *cmd,
		     G_GNUC_UNUSED WorkbookControl *wbc)
{
	CmdMergeData *me = CMD_MERGE_DATA (cmd);
	gboolean success = TRUE;

	g_slist_foreach (me->sheet_list, cmd_merge_data_delete_sheets, &success);
	g_slist_free (me->sheet_list);
	me->sheet_list = NULL;

	return FALSE;
}

static gboolean
cmd_merge_data_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdMergeData *me = CMD_MERGE_DATA (cmd);
	int i;
	GnmCellRegion *merge_content;
	GnmRangeRef *cell = &me->merge_zone->v_range.cell;
	GnmPasteTarget pt;
	GSList *this_field = me->merge_fields;
	GSList *this_data = me->merge_data;
	Sheet *source_sheet = cell->a.sheet;
	GSList *target_sheet;
	GnmRange target_range;
	ColRowStateList *state_col;
	ColRowStateList *state_row;

	range_init (&target_range, cell->a.col, cell->a.row,
		    cell->b.col, cell->b.row);
	merge_content = clipboard_copy_range (source_sheet, &target_range);
	state_col = colrow_get_states (source_sheet, TRUE, target_range.start.col,
					   target_range.end.col);
	state_row = colrow_get_states (source_sheet, FALSE, target_range.start.row,
					   target_range.end.row);

	for (i = 0; i < me->n; i++) {
		Sheet *new_sheet;

		new_sheet = workbook_sheet_add (me->sheet->workbook, NULL, FALSE);
		me->sheet_list = g_slist_prepend (me->sheet_list, new_sheet);

		colrow_set_states (new_sheet, TRUE, target_range.start.col, state_col);
		colrow_set_states (new_sheet, FALSE, target_range.start.row, state_row);
		sheet_object_clone_sheet (source_sheet, new_sheet, &target_range);
		clipboard_paste_region (merge_content,
			paste_target_init (&pt, new_sheet, &target_range, PASTE_ALL_TYPES),
			GNM_CMD_CONTEXT (wbc));
	}
	me->sheet_list = g_slist_reverse (me->sheet_list);
	colrow_state_list_destroy (state_col);
	colrow_state_list_destroy (state_row);

	while (this_field) {
		int col_source, row_source;
		int col_target, row_target;

		g_return_val_if_fail (this_data != NULL, TRUE);
		cell = &((GnmValue *)this_field->data)->v_range.cell;
		col_target = cell->a.col;
		row_target =  cell->a.row;

		cell = &((GnmValue *)this_data->data)->v_range.cell;
		col_source = cell->a.col;
		row_source =  cell->a.row;
		source_sheet = cell->a.sheet;

		target_sheet = me->sheet_list;
		while (target_sheet) {
			GnmCell *source_cell = sheet_cell_get (source_sheet,
							      col_source, row_source);
			if (source_cell == NULL) {
				GnmCell *target_cell = sheet_cell_get ((Sheet *)target_sheet->data,
								      col_target, row_target);
				if (target_cell != NULL)
					cell_assign_value (target_cell,
							   value_new_empty ());
			} else {
				GnmCell *target_cell = sheet_cell_fetch ((Sheet *)target_sheet->data,
								      col_target, row_target);
				cell_assign_value (target_cell,
						   value_dup (source_cell->value));
			}
			target_sheet = target_sheet->next;
			row_source++;
		}

		this_field = this_field->next;
		this_data = this_data->next;
	}

	return FALSE;
}

static void
cmd_merge_data_finalize (GObject *cmd)
{
	CmdMergeData *me = CMD_MERGE_DATA (cmd);

	value_release (me->merge_zone);
	me->merge_zone = NULL;
	range_list_destroy (me->merge_data);
	me->merge_data = NULL;
	range_list_destroy (me->merge_fields);
	me->merge_fields = NULL;
	g_slist_free (me->sheet_list);
	me->sheet_list = NULL;
	me->n = 0;

	gnm_command_finalize (cmd);
}

gboolean
cmd_merge_data (WorkbookControl *wbc, Sheet *sheet,
		GnmValue *merge_zone, GSList *merge_fields, GSList *merge_data)
{
	GObject       *obj;
	CmdMergeData *me;
	GnmRangeRef *cell;

	g_return_val_if_fail (IS_SHEET (sheet), TRUE);
	g_return_val_if_fail (merge_zone != NULL, TRUE);
	g_return_val_if_fail (merge_fields != NULL, TRUE);
	g_return_val_if_fail (merge_data != NULL, TRUE);

	obj = g_object_new (CMD_MERGE_DATA_TYPE, NULL);
	me = CMD_MERGE_DATA (obj);

	me->cmd.sheet = sheet;
	me->sheet = sheet;
	me->cmd.size = 1 + g_slist_length (merge_fields);
	me->cmd.cmd_descriptor =
		g_strdup_printf (_("Merging data into %s"), value_peek_string (merge_zone));

	me->merge_zone = merge_zone;
	me->merge_fields = merge_fields;
	me->merge_data = merge_data;
	me->sheet_list = NULL;

	cell = &((GnmValue *)merge_data->data)->v_range.cell;
	me->n = cell->b.row - cell->a.row + 1;

	/* Register the command object */
	return command_push_undo (wbc, obj);
}

/******************************************************************/

#define CMD_CHANGE_SUMMARY_TYPE        (cmd_change_summary_get_type ())
#define CMD_CHANGE_SUMMARY(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_CHANGE_SUMMARY_TYPE, CmdChangeSummary))

typedef struct {
	GnmCommand cmd;

	GSList *new_info;
	GSList *old_info;
} CmdChangeSummary;

MAKE_GNM_COMMAND (CmdChangeSummary, cmd_change_summary, NULL);

static void
cb_change_summary_apply_change (SummaryItem *sit, Workbook *wb)
{
	workbook_add_summary_info (wb, summary_item_copy (sit));
}

static gboolean
cmd_change_summary_apply (WorkbookControl *wbc, GSList *info)
{
	Workbook *wb = wb_control_workbook (wbc);

	g_slist_foreach (info, (GFunc) cb_change_summary_apply_change, wb);

	/* Set Workbook dirty!? */
	workbook_set_dirty (wb, TRUE);
	return FALSE;
}

static gboolean
cmd_change_summary_undo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdChangeSummary *me = CMD_CHANGE_SUMMARY (cmd);

	return cmd_change_summary_apply (wbc, me->old_info);
}

static gboolean
cmd_change_summary_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdChangeSummary *me = CMD_CHANGE_SUMMARY (cmd);

	return cmd_change_summary_apply (wbc, me->new_info);
}

static void
cb_change_summary_clear_sit (SummaryItem *sit,
			     G_GNUC_UNUSED gpointer ignore)
{
	summary_item_free (sit);
}

static void
cmd_change_summary_finalize (GObject *cmd)
{
	CmdChangeSummary *me = CMD_CHANGE_SUMMARY (cmd);

	g_slist_foreach (me->new_info, (GFunc) cb_change_summary_clear_sit, NULL);
	g_slist_free (me->new_info);
	me->new_info = NULL;

	g_slist_foreach (me->old_info, (GFunc) cb_change_summary_clear_sit, NULL);
	g_slist_free (me->old_info);
	me->old_info = NULL;

	gnm_command_finalize (cmd);
}

gboolean
cmd_change_summary (WorkbookControl *wbc, GSList *sin_changes)
{
	GObject          *obj;
	CmdChangeSummary *me;
	GSList           *sit_l;
	SummaryInfo const *sin = wb_control_workbook (wbc)->summary_info;

	if (sin_changes == NULL)
		return FALSE;

	obj = g_object_new (CMD_CHANGE_SUMMARY_TYPE, NULL);
	me = CMD_CHANGE_SUMMARY (obj);

	me->cmd.sheet = NULL;
	me->cmd.size = g_slist_length (sin_changes);
	me->cmd.cmd_descriptor =
		g_strdup_printf (_("Changing summary info"));

	me->new_info = sin_changes;

	me->old_info = NULL;
	for (sit_l = sin_changes; sit_l; sit_l = sit_l->next) {
		SummaryItem *sit = summary_item_by_name
			(((SummaryItem *)sit_l->data)->name, sin);
		if (sit == NULL)
			sit = summary_item_new_string  (((SummaryItem *)sit_l->data)->name,
							"", TRUE);
		me->old_info = g_slist_prepend (me->old_info, sit);
	}

	return command_push_undo (wbc, obj);
}

/******************************************************************/

#define CMD_OBJECT_RAISE_TYPE (cmd_object_raise_get_type ())
#define CMD_OBJECT_RAISE(o)   (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_OBJECT_RAISE_TYPE, CmdObjectRaise))

typedef struct {
	GnmCommand cmd;
	SheetObject *so;
	CmdObjectRaiseSelector dir;
	gint        changed_positions;
} CmdObjectRaise;

MAKE_GNM_COMMAND (CmdObjectRaise, cmd_object_raise, NULL);

static gboolean
cmd_object_raise_redo (GnmCommand *cmd,
		       G_GNUC_UNUSED WorkbookControl *wbc)
{
	CmdObjectRaise *me = CMD_OBJECT_RAISE (cmd);
	switch (me->dir) {
	case cmd_object_pull_to_front:
		me->changed_positions = sheet_object_adjust_stacking (me->so, 1);
		break;
	case cmd_object_pull_forward:
		me->changed_positions = sheet_object_adjust_stacking (me->so, G_MAXINT/2);
		break;
	case cmd_object_push_backward:
		me->changed_positions = sheet_object_adjust_stacking (me->so, -1);
		break;
	case cmd_object_push_to_back:
		me->changed_positions = sheet_object_adjust_stacking (me->so, G_MININT/2);
		break;
	}
	return FALSE;
}

static gboolean
cmd_object_raise_undo (GnmCommand *cmd,
		       G_GNUC_UNUSED WorkbookControl *wbc)
{
	CmdObjectRaise *me = CMD_OBJECT_RAISE (cmd);
	if (me->changed_positions != 0)
		sheet_object_adjust_stacking (me->so, - me->changed_positions);
	return FALSE;
}

static void
cmd_object_raise_finalize (GObject *cmd)
{
	CmdObjectRaise *me = CMD_OBJECT_RAISE (cmd);
	g_object_unref (G_OBJECT (me->so));
	gnm_command_finalize (cmd);
}

gboolean
cmd_object_raise (WorkbookControl *wbc, SheetObject *so, CmdObjectRaiseSelector dir)
{
	GObject *object;
	CmdObjectRaise *me;

	g_return_val_if_fail (IS_SHEET_OBJECT (so), TRUE);

	object = g_object_new (CMD_OBJECT_RAISE_TYPE, NULL);
	me = CMD_OBJECT_RAISE (object);

	me->so = so;
	g_object_ref (G_OBJECT (so));

	me->cmd.sheet = sheet_object_get_sheet (so);
	me->cmd.size = 1;
	switch (dir) {
	case cmd_object_pull_to_front:
		me->cmd.cmd_descriptor = g_strdup (_("Pull Object to the Front"));
		break;
	case cmd_object_pull_forward:
		me->cmd.cmd_descriptor = g_strdup (_("Pull Object Forward"));
		break;
	case cmd_object_push_backward:
		me->cmd.cmd_descriptor = g_strdup (_("Push Object Backward"));
		break;
	case cmd_object_push_to_back:
		me->cmd.cmd_descriptor = g_strdup (_("Push Object to the Back"));
		break;
	}
	me->dir = dir;
	me->changed_positions = 0;

	return command_push_undo (wbc, object);
}

/******************************************************************/

#define CMD_PRINT_SETUP_TYPE        (cmd_print_setup_get_type ())
#define CMD_PRINT_SETUP(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_PRINT_SETUP_TYPE, CmdPrintSetup))

typedef struct {
	GnmCommand cmd;

	GSList *old_pi;
	PrintInformation *new_pi;
} CmdPrintSetup;

MAKE_GNM_COMMAND (CmdPrintSetup, cmd_print_setup, NULL);

static gboolean
cmd_print_setup_undo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdPrintSetup *me = CMD_PRINT_SETUP (cmd);
	guint n, i;
	Workbook *book;
	GSList *infos;

	g_return_val_if_fail (me->old_pi != NULL, TRUE);

	if (me->cmd.sheet) {
		print_info_free (me->cmd.sheet->print_info);
		me->cmd.sheet->print_info = print_info_dup (
			(PrintInformation *) me->old_pi->data);
	} else {
		book = wb_control_workbook(wbc);
		n = workbook_sheet_count (book);
		infos = me->old_pi;
		g_return_val_if_fail (g_slist_length (infos) == n, TRUE);

		for (i = 0 ; i < n ; i++) {
			Sheet *sheet = workbook_sheet_by_index (book, i);

			g_return_val_if_fail (infos != NULL, TRUE);

			print_info_free (sheet->print_info);
			sheet->print_info = print_info_dup (
				(PrintInformation *) infos->data);
			infos = infos->next;
		}
	}
	return FALSE;
}

static gboolean
cmd_print_setup_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdPrintSetup *me = CMD_PRINT_SETUP (cmd);
	int n, i;
	Workbook *book;
	gboolean save_pis = (me->old_pi == NULL);

	if (me->cmd.sheet) {
		if (save_pis)
			me->old_pi = g_slist_append (me->old_pi, me->cmd.sheet->print_info);
		else
			print_info_free (me->cmd.sheet->print_info);
		me->cmd.sheet->print_info = print_info_dup (me->new_pi);
	} else {
		book = wb_control_workbook(wbc);
		n = workbook_sheet_count (book);
		for (i = 0 ; i < n ; i++) {
			Sheet * sheet = workbook_sheet_by_index (book, i);
			sheet_set_dirty (sheet, TRUE);
			if (save_pis)
				me->old_pi = g_slist_prepend (me->old_pi, sheet->print_info);
			else
				print_info_free (sheet->print_info);
			sheet->print_info = print_info_dup (me->new_pi);
		}
		me->old_pi = g_slist_reverse (me->old_pi);
	}
	return FALSE;
}

static void
cmd_print_setup_finalize (GObject *cmd)
{
	CmdPrintSetup *me = CMD_PRINT_SETUP (cmd);
	GSList *list = me->old_pi;

	if (me->new_pi)
		print_info_free (me->new_pi);
	for (; list; list = list->next)
		print_info_free ((PrintInformation *) list->data);
	g_slist_free (me->old_pi);
	gnm_command_finalize (cmd);
}

gboolean
cmd_print_setup (WorkbookControl *wbc, Sheet *sheet, PrintInformation const *pi)
{
	GObject          *obj;
	CmdPrintSetup *me;

	obj = g_object_new (CMD_PRINT_SETUP_TYPE, NULL);
	me = CMD_PRINT_SETUP (obj);

	me->cmd.sheet = sheet;
	me->cmd.size = 10;
	if (sheet)
		me->cmd.cmd_descriptor =
			g_strdup_printf (_("Page Setup For %s"), sheet->name_unquoted);
	else
		me->cmd.cmd_descriptor = g_strdup (_("Page Setup For All Sheets"));
	me->old_pi = NULL;
	me->new_pi = print_info_dup (pi);

	return command_push_undo (wbc, obj);
}

/******************************************************************/

#define CMD_DEFINE_NAME_TYPE        (cmd_define_name_get_type ())
#define CMD_DEFINE_NAME(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_DEFINE_NAME_TYPE, CmdDefineName))

typedef struct {
	GnmCommand cmd;

	GnmParsePos	 pp;
	char		*name;
	GnmExpr const	*expr;
	gboolean	 new_name;
	gboolean	 placeholder;
} CmdDefineName;

MAKE_GNM_COMMAND (CmdDefineName, cmd_define_name, NULL);

static gboolean
cmd_define_name_undo (GnmCommand *cmd,
		      G_GNUC_UNUSED WorkbookControl *wbc)
{
	CmdDefineName *me = CMD_DEFINE_NAME (cmd);
	GnmNamedExpr  *nexpr = expr_name_lookup (&(me->pp), me->name);
	GnmExpr const *expr = nexpr->expr;

	gnm_expr_ref (expr);
	if (me->new_name)
		expr_name_remove (nexpr);
	else if (me->placeholder)
		expr_name_downgrade_to_placeholder (nexpr);
	else
		expr_name_set_expr (nexpr, me->expr); /* restore old def */

	me->expr = expr;
	return FALSE;
}

static gboolean
cmd_define_name_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdDefineName *me = CMD_DEFINE_NAME (cmd);
	GnmNamedExpr  *nexpr = expr_name_lookup (&(me->pp), me->name);

	me->new_name = (nexpr == NULL);
	me->placeholder = (nexpr != NULL)
		&& expr_name_is_placeholder (nexpr);

	if (me->new_name || me->placeholder) {
		char *err = NULL;
		nexpr = expr_name_add (&me->pp, me->name, me->expr, &err, TRUE);
		if (nexpr == NULL) {
			gnm_cmd_context_error_invalid (GNM_CMD_CONTEXT (wbc), _("Name"), err);
			g_free (err);
			return TRUE;
		}
		me->expr = NULL;
	} else {	/* changing the definition */
		GnmExpr const *tmp = nexpr->expr;
		gnm_expr_ref (tmp);
		expr_name_set_expr (nexpr, me->expr);
		me->expr = tmp;	/* store the old definition */
	}

	return FALSE;
}

static void
cmd_define_name_finalize (GObject *cmd)
{
	CmdDefineName *me = CMD_DEFINE_NAME (cmd);

	g_free (me->name); me->name = NULL;

	if (me->expr != NULL) {
		gnm_expr_unref (me->expr);
		me->expr = NULL;
	}

	gnm_command_finalize (cmd);
}

/**
 * cmd_define_name :
 * @wbc :
 * @name :
 * @pp   :
 * @expr : absorbs a ref to the expr.
 *
 * If the @name has never been defined in context @pp create a new name
 * If its a placeholder assign @expr to it and make it real
 * If it already exists as a real name just assign @expr.
 *
 * Returns TRUE on error
 **/
gboolean
cmd_define_name (WorkbookControl *wbc, char const *name,
		 GnmParsePos const *pp, GnmExpr const *expr)
{
	GObject		*obj;
	CmdDefineName	*me;
	GnmNamedExpr    *nexpr;

	g_return_val_if_fail (name != NULL, TRUE);
	g_return_val_if_fail (pp != NULL, TRUE);
	g_return_val_if_fail (expr != NULL, TRUE);

	if (expr_name_check_for_loop (name, expr)) {
		gnm_cmd_context_error_invalid (GNM_CMD_CONTEXT (wbc), name,
					_("has a circular reference"));
		gnm_expr_unref (expr);
		return TRUE;
	}
	nexpr = expr_name_lookup (pp, name);
	if (nexpr != NULL && !expr_name_is_placeholder (nexpr) &&
	    gnm_expr_equal (expr, nexpr->expr)) {
		gnm_expr_unref (expr);
		return FALSE; /* expr is not changing, do nothing */
	}

	obj = g_object_new (CMD_DEFINE_NAME_TYPE, NULL);
	me = CMD_DEFINE_NAME (obj);
	me->name = g_strdup (name);
	me->pp = *pp;
	me->expr = expr;

	me->cmd.sheet = wb_control_cur_sheet (wbc);
	me->cmd.size = 1;

	nexpr = expr_name_lookup (pp, name);
	if (nexpr == NULL || expr_name_is_placeholder (nexpr))
		me->cmd.cmd_descriptor =
			g_strdup_printf (_("Define Name %s"), name);
	else
		me->cmd.cmd_descriptor =
			g_strdup_printf (_("Update Name %s"), name);

	return command_push_undo (wbc, obj);
}

/******************************************************************/

#define CMD_SCENARIO_ADD_TYPE (cmd_scenario_add_get_type ())
#define CMD_SCENARIO_ADD(o)   (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_SCENARIO_ADD_TYPE, CmdScenarioAdd))

typedef struct {
	GnmCommand cmd;
	scenario_t     *scenario;
} CmdScenarioAdd;

MAKE_GNM_COMMAND (CmdScenarioAdd, cmd_scenario_add, NULL);

static gboolean
cmd_scenario_add_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdScenarioAdd *me = CMD_SCENARIO_ADD (cmd);

	scenario_add (me->cmd.sheet,
		      scenario_copy (me->scenario, me->cmd.sheet));

	return FALSE;
}

static gboolean
cmd_scenario_add_undo (GnmCommand *cmd,
			G_GNUC_UNUSED WorkbookControl *wbc)
{
	CmdScenarioAdd *me = CMD_SCENARIO_ADD (cmd);

	me->cmd.sheet->scenarios = scenario_delete (me->cmd.sheet->scenarios,
						    me->scenario->name);

	return FALSE;
}

static void
cmd_scenario_add_finalize (GObject *cmd)
{
	CmdScenarioAdd *me = CMD_SCENARIO_ADD (cmd);

	scenario_free (me->scenario);
	gnm_command_finalize (cmd);
}

gboolean
cmd_scenario_add (WorkbookControl *wbc, scenario_t *s, Sheet *sheet)
{
	CmdScenarioAdd *me;
	GObject        *object;

	g_return_val_if_fail (IS_WORKBOOK_CONTROL (wbc), TRUE);
	g_return_val_if_fail (IS_SHEET (sheet), TRUE);

	object = g_object_new (CMD_SCENARIO_ADD_TYPE, NULL);
	me = CMD_SCENARIO_ADD (object);

	me->scenario  = s;
	me->cmd.sheet = sheet;
	me->cmd.size  = 1;
	me->cmd.cmd_descriptor = g_strdup (_("Add scenario"));

	return command_push_undo (wbc, object);
}

/******************************************************************/

#define CMD_SCENARIO_MNGR_TYPE (cmd_scenario_mngr_get_type ())
#define CMD_SCENARIO_MNGR(o)   (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_SCENARIO_MNGR_TYPE, CmdScenarioMngr))

typedef struct {
	GnmCommand cmd;
	scenario_cmd_t  *sc;
} CmdScenarioMngr;

MAKE_GNM_COMMAND (CmdScenarioMngr, cmd_scenario_mngr, NULL);

static gboolean
cmd_scenario_mngr_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdScenarioMngr *me = CMD_SCENARIO_MNGR (cmd);
	data_analysis_output_t dao;

	dao_init (&dao, NewSheetOutput);
	dao.sheet = me->cmd.sheet;
	scenario_free (me->sc->undo);
	me->sc->undo = scenario_show (wbc, me->sc->redo, NULL, &dao);

	return FALSE;
}

static gboolean
cmd_scenario_mngr_undo (GnmCommand *cmd,
			G_GNUC_UNUSED WorkbookControl *wbc)
{
	CmdScenarioMngr *me = CMD_SCENARIO_MNGR (cmd);
	scenario_t      *tmp;
	data_analysis_output_t dao;

	dao_init (&dao, NewSheetOutput);
	dao.sheet = me->cmd.sheet;
	tmp = scenario_copy (me->sc->undo, dao.sheet);
	scenario_show (wbc, NULL, tmp, &dao);

	return FALSE;
}

static void
cmd_scenario_mngr_finalize (GObject *cmd)
{
	CmdScenarioMngr *me = CMD_SCENARIO_MNGR (cmd);

	scenario_free (me->sc->undo);
	scenario_free (me->sc->redo);
	g_free (me->sc);

	gnm_command_finalize (cmd);
}

gboolean
cmd_scenario_mngr (WorkbookControl *wbc, scenario_cmd_t *sc, Sheet *sheet)
{
	CmdScenarioMngr *me;
	GObject         *object;
	data_analysis_output_t dao;

	g_return_val_if_fail (IS_WORKBOOK_CONTROL (wbc), TRUE);
	g_return_val_if_fail (IS_SHEET (sheet), TRUE);

	object = g_object_new (CMD_SCENARIO_MNGR_TYPE, NULL);
	me     = CMD_SCENARIO_MNGR (object);

	me->sc = sc;
	me->cmd.sheet = sheet;
	me->cmd.size  = 1;
	me->cmd.cmd_descriptor = g_strdup (_("Scenario Show"));

	dao_init (&dao, NewSheetOutput);
	dao.sheet = me->cmd.sheet;
	me->sc->redo = scenario_show (wbc, me->sc->undo, NULL, &dao);

	return command_push_undo (wbc, object);
}

/******************************************************************/

#define CMD_DATA_SHUFFLE_TYPE (cmd_data_shuffle_get_type ())
#define CMD_DATA_SHUFFLE(o)   (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_DATA_SHUFFLE_TYPE, CmdDataShuffle))

typedef struct {
	GnmCommand  cmd;
	data_shuffling_t *ds;
} CmdDataShuffle;

MAKE_GNM_COMMAND (CmdDataShuffle, cmd_data_shuffle, NULL);

static gboolean
cmd_data_shuffle_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdDataShuffle *me = CMD_DATA_SHUFFLE (cmd);

	data_shuffling_redo (me->ds);
	return FALSE;
}

static gboolean
cmd_data_shuffle_undo (GnmCommand *cmd,
		       G_GNUC_UNUSED WorkbookControl *wbc)
{
	CmdDataShuffle *me = CMD_DATA_SHUFFLE (cmd);

	data_shuffling_redo (me->ds);
	return FALSE;
}

static void
cmd_data_shuffle_finalize (GObject *cmd)
{
	CmdDataShuffle *me = CMD_DATA_SHUFFLE (cmd);

	data_shuffling_free (me->ds);
	gnm_command_finalize (cmd);
}

gboolean
cmd_data_shuffle (WorkbookControl *wbc, data_shuffling_t *sc, Sheet *sheet)
{
	CmdDataShuffle *me;
	GObject         *object;

	g_return_val_if_fail (IS_WORKBOOK_CONTROL (wbc), TRUE);
	g_return_val_if_fail (IS_SHEET (sheet), TRUE);

	object = g_object_new (CMD_DATA_SHUFFLE_TYPE, NULL);
	me     = CMD_DATA_SHUFFLE (object);

	me->ds        = sc;
	me->cmd.sheet = sheet;
	me->cmd.size  = 1;
	me->cmd.cmd_descriptor = g_strdup (_("Shuffle Data"));

	return command_push_undo (wbc, object);
}

/******************************************************************/

#define CMD_TEXT_TO_COLUMNS_TYPE        (cmd_text_to_columns_get_type ())
#define CMD_TEXT_TO_COLUMNS(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_TEXT_TO_COLUMNS_TYPE, CmdTextToColumns))

typedef struct {
	GnmCommand cmd;

	GnmCellRegion      *content;
	GnmPasteTarget      dst;
	GnmRange            src;
	Sheet           *src_sheet;
	ColRowStateList *saved_sizes;
} CmdTextToColumns;

MAKE_GNM_COMMAND (CmdTextToColumns, cmd_text_to_columns, NULL);

static gboolean
cmd_text_to_columns_impl (GnmCommand *cmd, WorkbookControl *wbc,
		     gboolean is_undo)
{
	CmdTextToColumns *me = CMD_TEXT_TO_COLUMNS (cmd);
	GnmCellRegion *content;
	SheetView *sv;

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->content != NULL, TRUE);

	content = clipboard_copy_range (me->dst.sheet, &me->dst.range);
	if (clipboard_paste_region (me->content, &me->dst, GNM_CMD_CONTEXT (wbc))) {
		/* There was a problem, avoid leaking */
		cellregion_unref (content);
		return TRUE;
	}

	cellregion_unref (me->content);

	if (is_undo) {
		colrow_set_states (me->dst.sheet, FALSE,
			me->dst.range.start.row, me->saved_sizes);
		colrow_state_list_destroy (me->saved_sizes);
		me->saved_sizes = NULL;
	} else {
		me->saved_sizes = colrow_get_states (me->dst.sheet,
			FALSE, me->dst.range.start.row, me->dst.range.end.row);
		rows_height_update (me->dst.sheet, &me->dst.range, FALSE);
	}

	me->content = content;

	/* Make the newly pasted content the selection (this queues a redraw) */
	sv = sheet_get_view (me->dst.sheet, wb_control_view (wbc));
	sv_selection_reset (sv);
	sv_selection_add_range (sv,
		me->dst.range.start.col, me->dst.range.start.row,
		me->dst.range.start.col, me->dst.range.start.row,
		me->dst.range.end.col, me->dst.range.end.row);
	sv_make_cell_visible (sv,
		me->dst.range.start.col, me->dst.range.start.row, FALSE);

	return FALSE;
}

static gboolean
cmd_text_to_columns_undo (GnmCommand *cmd, WorkbookControl *wbc)
{
	return cmd_text_to_columns_impl (cmd, wbc, TRUE);
}

static gboolean
cmd_text_to_columns_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	return cmd_text_to_columns_impl (cmd, wbc, FALSE);
}

static void
cmd_text_to_columns_finalize (GObject *cmd)
{
	CmdTextToColumns *me = CMD_TEXT_TO_COLUMNS (cmd);

	if (me->saved_sizes)
		me->saved_sizes = colrow_state_list_destroy (me->saved_sizes);
	if (me->content) {
		cellregion_unref (me->content);
		me->content = NULL;
	}
	gnm_command_finalize (cmd);
}

gboolean
cmd_text_to_columns (WorkbookControl *wbc,
		     GnmRange const *src, Sheet *src_sheet, 
		     GnmRange const *target, Sheet *target_sheet, 
		     GnmCellRegion *content)
{
	GObject *obj;
	CmdTextToColumns *me;

	g_return_val_if_fail (content != NULL, TRUE);

	obj = g_object_new (CMD_TEXT_TO_COLUMNS_TYPE, NULL);
	me = CMD_TEXT_TO_COLUMNS (obj);

	me->cmd.sheet = (src_sheet == target_sheet ? src_sheet : NULL);
	me->cmd.size = 1;  /* FIXME?  */
	me->cmd.cmd_descriptor = g_strdup_printf (_("Text (%s) to Columns (%s)"),
						  undo_global_range_name (src_sheet, src),
						  undo_global_range_name (target_sheet, target));
	me->dst.range = *target;
	me->dst.sheet = target_sheet;
	me->dst.paste_flags = PASTE_CONTENT | PASTE_FORMATS;
	me->src = *src;
	me->src_sheet = src_sheet;
	me->content = content;
	me->saved_sizes = NULL;

	/* Check array subdivision & merged regions */
	if (sheet_range_splits_region (target_sheet, &me->dst.range,
				       NULL, GNM_CMD_CONTEXT (wbc), me->cmd.cmd_descriptor)) {
		g_object_unref (G_OBJECT (me));
		return TRUE;
	}

	return command_push_undo (wbc, obj);
}

/******************************************************************/

#define CMD_SOLVER_TYPE        (cmd_solver_get_type ())
#define CMD_SOLVER(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_SOLVER_TYPE, CmdSolver))

typedef struct {
	GnmCommand cmd;

	GSList	  *cells;
	GSList	  *ov;
	GSList	  *nv;
} CmdSolver;

MAKE_GNM_COMMAND (CmdSolver, cmd_solver, NULL);

static gboolean
cmd_solver_impl (GSList *cell_stack, GSList *value_stack)
{
	while (cell_stack != NULL &&  value_stack != NULL) {
		GSList *values = value_stack->data;
		GSList *cells  = cell_stack->data;

		while (values != NULL) {
			char const *str = values->data;
			GnmCell *cell = cells->data;
			
			if (cell != NULL) {
				sheet_cell_set_text (cell, str, NULL);
				cells = cells->next;
			}
			values = values->next;
		}
		value_stack = value_stack->next;
		cell_stack = cell_stack->next;
	}
	return FALSE;
}


static gboolean
cmd_solver_undo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdSolver *me = CMD_SOLVER (cmd);

	return cmd_solver_impl (me->cells, me->ov);;
}

static gboolean
cmd_solver_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdSolver *me = CMD_SOLVER (cmd);

	return cmd_solver_impl (me->cells, me->nv);;
}

static void
cmd_solver_free_values (GSList *v, G_GNUC_UNUSED gpointer user_data)
{
	g_slist_foreach (v, (GFunc)g_free, NULL);
	g_slist_free (v);
}

static void
cmd_solver_finalize (GObject *cmd)
{
	CmdSolver *me = CMD_SOLVER (cmd);

	g_slist_free (me->cells);
	me->cells = NULL;
	g_slist_foreach (me->ov, (GFunc)cmd_solver_free_values,
			 NULL);
	g_slist_free (me->ov);
	me->ov = NULL;
	g_slist_foreach (me->nv, (GFunc)cmd_solver_free_values,
			 NULL);
	g_slist_free (me->nv);
	me->nv = NULL;

	gnm_command_finalize (cmd);
}

static GSList *
cmd_solver_get_cell_values (GSList *cell_stack)
{
	GSList *value_stack = NULL;
	
	while (cell_stack != NULL) {
		GSList *cells  = cell_stack->data;
		GSList *values = NULL;
		while (cells != NULL) {
			GnmCell *the_Cell = (GnmCell *)(cells->data);
			if (the_Cell != NULL)
				values = g_slist_append 
					(values, 
					 value_get_as_string 
					 (the_Cell->value));
			else
				values = g_slist_append 
					(values, NULL);
			cells = cells->next;
		}
		value_stack = g_slist_append (value_stack, 
					       values);
		cell_stack = cell_stack->next;
	}
	
	return value_stack;
}

gboolean
cmd_solver (WorkbookControl *wbc, GSList *cells, GSList *ov, GSList *nv)
{
	GObject *obj;
	CmdSolver *me;

	g_return_val_if_fail (cells != NULL, TRUE);
	g_return_val_if_fail (ov != NULL || nv != NULL, TRUE);

	obj = g_object_new (CMD_SOLVER_TYPE, NULL);
	me = CMD_SOLVER (obj);

	me->cmd.sheet = NULL;
	me->cmd.size = g_slist_length (cells);
	me->cmd.cmd_descriptor = g_strdup_printf (_("Solver"));

	me->cells = cells;
	me->ov = ov;
	me->nv = nv;

	if (me->ov == NULL)
		me->ov = cmd_solver_get_cell_values (cells);
	if (me->nv == NULL)
		me->nv = cmd_solver_get_cell_values (cells);

	return command_push_undo (wbc, obj);
}

/******************************************************************/

#define CMD_GOAL_SEEK_TYPE        (cmd_goal_seek_get_type ())
#define CMD_GOAL_SEEK(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_GOAL_SEEK_TYPE, CmdGoalSeek))

typedef struct {
	GnmCommand cmd;

	GnmCell	  *cell;
	GnmValue  *ov;
	GnmValue  *nv;
} CmdGoalSeek;

MAKE_GNM_COMMAND (CmdGoalSeek, cmd_goal_seek, NULL);

static gboolean
cmd_goal_seek_impl (GnmCell *cell, GnmValue *value)
{
	sheet_cell_set_value (cell, value_dup(value));
	workbook_recalc (cell->base.sheet->workbook);
	return FALSE;
}


static gboolean
cmd_goal_seek_undo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdGoalSeek *me = CMD_GOAL_SEEK (cmd);

	return cmd_goal_seek_impl (me->cell, me->ov);
}

static gboolean
cmd_goal_seek_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdGoalSeek *me = CMD_GOAL_SEEK (cmd);

	return cmd_goal_seek_impl (me->cell, me->nv);
}

static void
cmd_goal_seek_finalize (GObject *cmd)
{
	CmdGoalSeek *me = CMD_GOAL_SEEK (cmd);

	value_release (me->ov);
	me->ov = NULL;
	value_release (me->nv);
	me->nv = NULL;

	gnm_command_finalize (cmd);
}

gboolean
cmd_goal_seek (WorkbookControl *wbc, GnmCell *cell, GnmValue *ov, GnmValue *nv)
{
	GObject *obj;
	CmdGoalSeek *me;
	GnmRange range;

	g_return_val_if_fail (cell != NULL, TRUE);
	g_return_val_if_fail (ov != NULL || nv != NULL, TRUE);

	obj = g_object_new (CMD_GOAL_SEEK_TYPE, NULL);
	me = CMD_GOAL_SEEK (obj);

	me->cmd.sheet = cell->base.sheet;
	me->cmd.size = 1;
	range_init_cellpos (&range, &cell->pos, &cell->pos);
	me->cmd.cmd_descriptor = g_strdup_printf 
		(_("Goal Seek (%s)"), undo_global_range_name (cell->base.sheet, &range));

	me->cell = cell;
	me->ov = ov;
	me->nv = nv;

	if (me->ov == NULL)
		me->ov = value_dup (cell->value);
	if (me->nv == NULL)
		me->nv = value_dup (cell->value);

	return command_push_undo (wbc, obj);
}

/******************************************************************/

#if 0
#define CMD_FREEZE_PANES_TYPE        (cmd_freeze_panes_get_type ())
#define CMD_FREEZE_PANES(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_FREEZE_PANES_TYPE, CmdFreezePanes))

typedef struct {
	GnmCommand cmd;

	SheetView *sv;
	GnmCellPos	   pos;
} CmdFreezePanes;

MAKE_GNM_COMMAND (CmdFreezePanes, cmd_freeze_panes, NULL);

static gboolean
cmd_freeze_panes_undo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdFreezePanes *me = CMD_FREEZE_PANES (cmd);

	return FALSE;
}

static gboolean
cmd_freeze_panes_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdFreezePanes *me = CMD_FREEZE_PANES (cmd);

	return FALSE;
}

static void
cmd_freeze_panes_finalize (GObject *cmd)
{
	CmdFreezePanes *me = CMD_FREEZE_PANES (cmd);

	gnm_command_finalize (cmd);
}

/**
 * cmd_freeze_panes :
 * @wbc : where to report errors
 * @sv  : the view to freeze
 * @frozen   :
 * @unfrozen :
 *
 * Returns TRUE on error
 **/
gboolean
cmd_freeze_panes (WorkbookControl *wbc, SheetView *sv,
		  GnmCellPos const *frozen, GnmCellPos const *unfrozen)
{
	GObject		*obj;
	CmdFreezePanes	*me;

	g_return_val_if_fail (name != NULL, TRUE);
	g_return_val_if_fail (pp != NULL, TRUE);
	g_return_val_if_fail (expr != NULL, TRUE);

	obj = g_object_new (CMD_FREEZE_PANES_TYPE, NULL);
	me = CMD_FREEZE_PANES (obj);
	me->sv = sv;
	me->frozen   = f;
	me->unfrozen = expr;
	return command_push_undo (wbc, obj);
}

#endif


/******************************************************************/


#define CMD_CLONE_SHEET_TYPE        (cmd_clone_sheet_get_type ())
#define CMD_CLONE_SHEET(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_CLONE_SHEET_TYPE, CmdCloneSheet))

typedef struct {
	GnmCommand cmd;
	Sheet *new_sheet;
} CmdCloneSheet;

static void
cmd_clone_sheet_repeat (GnmCommand const *cmd, WorkbookControl *wbc)
{
	cmd_clone_sheet (wbc, wb_control_cur_sheet (wbc));
}
MAKE_GNM_COMMAND (CmdCloneSheet, cmd_clone_sheet, cmd_clone_sheet_repeat);

static gboolean
cmd_clone_sheet_undo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdCloneSheet *me = CMD_CLONE_SHEET (cmd);
	return !command_undo_sheet_delete (me->new_sheet);
}

static gboolean
cmd_clone_sheet_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdCloneSheet *me = CMD_CLONE_SHEET (cmd);
     	
	me->new_sheet = sheet_dup (me->cmd.sheet);

	workbook_sheet_attach (me->cmd.sheet->workbook, me->new_sheet, 
			       me->cmd.sheet);
	workbook_set_dirty (me->new_sheet->workbook, TRUE);
	wbcg_focus_cur_scg (WORKBOOK_CONTROL_GUI(wbc));

	return FALSE;
}

static void
cmd_clone_sheet_finalize (GObject *cmd)
{
	gnm_command_finalize (cmd);
}

gboolean
cmd_clone_sheet (WorkbookControl *wbc, Sheet *sheet)
{
	GObject       *obj;
	CmdCloneSheet *me;

	g_return_val_if_fail (IS_SHEET (sheet), TRUE);

	obj = g_object_new (CMD_CLONE_SHEET_TYPE, NULL);
	me = CMD_CLONE_SHEET (obj);

	me->cmd.sheet = sheet;
	me->cmd.size = 1;

	me->cmd.cmd_descriptor =
		g_strdup_printf (_("Duplicating %s"), sheet->name_unquoted);

	return command_push_undo (wbc, obj);
}
/******************************************************************/


#define CMD_TABULATE_TYPE        (cmd_tabulate_get_type ())
#define CMD_TABULATE(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_TABULATE_TYPE, CmdTabulate))

typedef struct {
	GnmCommand cmd;
	GSList *sheet_idx;
	GnmTabulateInfo *data;
} CmdTabulate;

MAKE_GNM_COMMAND (CmdTabulate, cmd_tabulate, NULL);

static gboolean
cmd_tabulate_undo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdTabulate *me = CMD_TABULATE (cmd);
	GSList *l;
	gboolean res = TRUE;

	me->sheet_idx  = g_slist_sort (me->sheet_idx,
				       cmd_reorganize_sheets_delete_cmp_f);

	for (l = me->sheet_idx; l != NULL; l = l->next) {
		Sheet *new_sheet 
			= workbook_sheet_by_index (wb_control_workbook (wbc), 
						   GPOINTER_TO_INT (l->data));
		res = res && command_undo_sheet_delete (new_sheet);
	}
	return !res;
}

static gboolean
cmd_tabulate_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdTabulate *me = CMD_TABULATE (cmd);

	if (me->sheet_idx != NULL) {
		g_slist_free (me->sheet_idx);
		me->sheet_idx = NULL;
	}

	me->sheet_idx = do_tabulation (wbc, me->data);

	return (me->sheet_idx == NULL);
}

static void
cmd_tabulate_finalize (GObject *cmd)
{
	CmdTabulate *me = CMD_TABULATE (cmd);

	g_free (me->data->cells);
	g_free (me->data->minima);
	g_free (me->data->maxima);
	g_free (me->data->steps);
	g_free (me->data);
	gnm_command_finalize (cmd);
}

gboolean
cmd_tabulate (WorkbookControl *wbc, gpointer data)
{
	GObject       *obj;
	CmdTabulate *me;

	g_return_val_if_fail (data != NULL, TRUE);

	obj = g_object_new (CMD_TABULATE_TYPE, NULL);
	me = CMD_TABULATE (obj);

	me->cmd.sheet = NULL;
	me->cmd.size = 1;
	me->cmd.cmd_descriptor =
		g_strdup_printf (_("Tabulating Dependencies"));
	me->data = data;
	me->sheet_idx = NULL;

	return command_push_undo (wbc, obj);
}

/******************************************************************/


#define CMD_SO_GRAPH_CONFIG_TYPE (cmd_so_graph_config_get_type ())
#define CMD_SO_GRAPH_CONFIG(o)   (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_SO_GRAPH_CONFIG_TYPE, CmdSOGraphConfig))

typedef struct {
	GnmCommand cmd;
	SheetObject *so;
	GogGraph *new_graph;
	GogGraph *old_graph;
} CmdSOGraphConfig;

MAKE_GNM_COMMAND (CmdSOGraphConfig, cmd_so_graph_config, NULL);

static gboolean
cmd_so_graph_config_redo (GnmCommand *cmd,
			  G_GNUC_UNUSED WorkbookControl *wbc)
{
	CmdSOGraphConfig *me = CMD_SO_GRAPH_CONFIG (cmd);
	sheet_object_graph_set_gog (me->so, me->new_graph);
	return FALSE;
}

static gboolean
cmd_so_graph_config_undo (GnmCommand *cmd,
			  G_GNUC_UNUSED WorkbookControl *wbc)
{
	CmdSOGraphConfig *me = CMD_SO_GRAPH_CONFIG (cmd);
	sheet_object_graph_set_gog (me->so, me->old_graph);
	return FALSE;
}

static void
cmd_so_graph_config_finalize (GObject *cmd)
{
	CmdSOGraphConfig *me = CMD_SO_GRAPH_CONFIG (cmd);

	g_object_unref (G_OBJECT (me->so));
	g_object_unref (G_OBJECT (me->new_graph));
	g_object_unref (G_OBJECT (me->old_graph));

	gnm_command_finalize (cmd);
}

gboolean
cmd_so_graph_config (WorkbookControl *wbc, SheetObject *so,  
		     GObject *n_graph, GObject *o_graph)
{
	CmdSOGraphConfig *me;

	g_return_val_if_fail (IS_WORKBOOK_CONTROL (wbc), TRUE);
	g_return_val_if_fail (IS_SHEET_OBJECT_GRAPH (so), TRUE);
	g_return_val_if_fail (IS_GOG_GRAPH (n_graph), TRUE);
	g_return_val_if_fail (IS_GOG_GRAPH (o_graph), TRUE);
	
	me = g_object_new (CMD_SO_GRAPH_CONFIG_TYPE, NULL);

	me->so = so;
	g_object_ref (G_OBJECT (so));

	me->new_graph = GOG_GRAPH (n_graph);
	g_object_ref (G_OBJECT (me->new_graph));
	me->old_graph = GOG_GRAPH (o_graph);
	g_object_ref (G_OBJECT (me->old_graph));

	me->cmd.sheet = sheet_object_get_sheet (so);;
	me->cmd.size = 10;
	me->cmd.cmd_descriptor = g_strdup (_("Reconfigure Graph"));

	return command_push_undo (wbc, G_OBJECT (me));
}

/******************************************************************/

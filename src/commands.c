/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * commands.c: Handlers to undo & redo commands
 *
 * Copyright (C) 1999-2006 Jody Goldberg (jody@gnome.org)
 * Copyright (C) 2002-2007 Morten Welinder (terra@gnome.org)
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include "gnumeric.h"
#include "commands.h"

#include "application.h"
#include "sheet.h"
#include "sheet-view.h"
#include "sheet-style.h"
#include "gnm-format.h"
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
#include "auto-format.h"
#include "tools/dao.h"
#include "gnumeric-gconf.h"
#include "scenarios.h"
#include "data-shuffling.h"
#include "tools/tabulate.h"

#include <gsf/gsf-impl-utils.h>
#include <gsf/gsf-doc-meta-data.h>
#include <string.h>
#include <goffice/graph/gog-graph.h>
#include <goffice/utils/go-glib-extras.h>
#include <goffice/utils/go-pango-extras.h>

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

	/* primary sheet associated with command, or NULL.  */
	Sheet *sheet;

	/* See truncate_undo_info.  */
	int size;

	/* A string to put in the menu */
	char const *cmd_descriptor;

	/* State of workbook before the commands was undo.  */
	gboolean workbook_modified_before_do;
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


static guint
max_descriptor_width (void)
{
	return gnm_app_prefs->max_descriptor_width;
}

static char *
make_undo_text (char const *src, gboolean *truncated)
{
	char *dst = g_strdup (src);
	char *p;
	int len;
	int max_len = max_descriptor_width ();
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
				if (gnm_style_get_contents_locked (sheet_style_get (sheet, j, i))) {
					char *r = global_range_name (sheet, range);
					char *text = g_strdup_printf (wbv->is_protected  ?
						_("%s is locked. Unprotect the workbook to enable editing.") :
						_("%s is locked. Unprotect the sheet to enable editing."),
						r);
					go_cmd_context_error_invalid (GO_CMD_CONTEXT (wbc),
						cmd_name, text);
					g_free (text);
					g_free (r);
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

/*
 * A helper routine to select a range and make sure the top-left
 * is visible.
 */
static void
select_range (Sheet *sheet, const GnmRange *r, WorkbookControl *wbc)
{
	SheetView *sv = sheet_get_view (sheet, wb_control_view (wbc));

	wb_control_sheet_focus (wbc, sheet);
	sv_selection_reset (sv);
	sv_selection_add_range (sv, r);
	sv_make_cell_visible (sv, r->start.col, r->start.row, FALSE);
}

/*
 * A helper routine to select a list of ranges and make sure the top-left
 * corner of the last is visible.
 */
static void
select_selection (Sheet *sheet, GSList *selection, WorkbookControl *wbc)
{
	SheetView *sv = sheet_get_view (sheet, wb_control_view (wbc));
	const GnmRange *r0 = NULL;
	GSList *l;

	g_return_if_fail (selection != NULL);

	wb_control_sheet_focus (wbc, sheet);
	sv_selection_reset (sv);
	for (l = selection; l; l = l->next) {
		GnmRange const *r = l->data;
		sv_selection_add_range (sv, r);
		r0 = r;
	}
	sv_make_cell_visible (sv, r0->start.col, r0->start.row, FALSE);
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
update_after_action (Sheet *sheet, WorkbookControl *wbc)
{
	if (sheet != NULL) {
		g_return_if_fail (IS_SHEET (sheet));

		sheet_mark_dirty (sheet);
		if (workbook_get_recalcmode (sheet->workbook))
			workbook_recalc (sheet->workbook);
		sheet_update (sheet);

		if (sheet->workbook == wb_control_get_workbook (wbc))
			WORKBOOK_VIEW_FOREACH_CONTROL (wb_control_view (wbc), control,
				  wb_control_sheet_focus (control, sheet););
	} else if (wbc != NULL)
		sheet_update (wb_control_cur_sheet (wbc));
}


/**
 * command_undo : Undo the last command executed.
 * @wbc : The workbook control which issued the request.
 *        Any user level errors generated by undoing will be reported
 *        here.
 *
 * @wb : The workbook whose commands to undo.
 **/
void
command_undo (WorkbookControl *wbc)
{
	GnmCommand *cmd;
	GnmCommandClass *klass;
	Workbook *wb = wb_control_get_workbook (wbc);

	g_return_if_fail (wb != NULL);
	g_return_if_fail (wb->undo_commands != NULL);

	cmd = GNM_COMMAND (wb->undo_commands->data);
	g_return_if_fail (cmd != NULL);

	klass = CMD_CLASS (cmd);
	g_return_if_fail (klass != NULL);

	g_object_ref (cmd);

	/* TRUE indicates a failure to undo.  Leave the command where it is */
	if (!klass->undo_cmd (cmd, wbc)) {
		gboolean undo_cleared;

		update_after_action (cmd->sheet, wbc);

		if (!cmd->workbook_modified_before_do)
			go_doc_set_dirty (GO_DOC (wb), FALSE);

		/*
		 * A few commands clear the undo queue.  For those, we do not
		 * want to stuff the cmd object on the redo queue.
		 */
		undo_cleared = (wb->undo_commands == NULL);

		if (!undo_cleared) {
			wb->undo_commands = g_slist_remove (wb->undo_commands, cmd);
			wb->redo_commands = g_slist_prepend (wb->redo_commands, cmd);

			WORKBOOK_FOREACH_CONTROL (wb, view, control, {
				wb_control_undo_redo_pop (control, TRUE);
				wb_control_undo_redo_push (control, FALSE, cmd->cmd_descriptor, cmd);
			});
			undo_redo_menu_labels (wb);
			/* TODO : Should we mark the workbook as clean or pristine too */
		}
	}

	g_object_unref (cmd);
}

/**
 * command_redo : Redo the last command that was undone.
 * @wbc : The workbook control which issued the request.
 *        Any user level errors generated by redoing will be reported
 *        here.
 **/
void
command_redo (WorkbookControl *wbc)
{
	GnmCommand *cmd;
	GnmCommandClass *klass;
	Workbook *wb = wb_control_get_workbook (wbc);

	g_return_if_fail (wb);
	g_return_if_fail (wb->redo_commands);

	cmd = GNM_COMMAND (wb->redo_commands->data);
	g_return_if_fail (cmd != NULL);

	klass = CMD_CLASS (cmd);
	g_return_if_fail (klass != NULL);

	g_object_ref (cmd);

	cmd->workbook_modified_before_do =
		go_doc_is_dirty (wb_control_get_doc (wbc));

	/* TRUE indicates a failure to redo.  Leave the command where it is */
	if (!klass->redo_cmd (cmd, wbc)) {
		gboolean redo_cleared;

		update_after_action (cmd->sheet, wbc);

		/*
		 * A few commands clear the undo queue.  For those, we do not
		 * want to stuff the cmd object on the redo queue.
		 */
		redo_cleared = (wb->redo_commands == NULL);

		if (!redo_cleared) {
			wb->redo_commands = g_slist_remove (wb->redo_commands, cmd);
			wb->undo_commands = g_slist_prepend (wb->undo_commands, cmd);

			WORKBOOK_FOREACH_CONTROL (wb, view, control, {
				wb_control_undo_redo_push (control, TRUE, cmd->cmd_descriptor, cmd);
				wb_control_undo_redo_pop (control, FALSE);
			});
			undo_redo_menu_labels (wb);
		}
	}

	g_object_unref (cmd);
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
	Workbook *wb = wb_control_get_workbook (wbc);

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
	GSList *ptr, *tmp, *ignore;
	Workbook *wb = wb_control_get_workbook (wbc);

	g_return_if_fail (wb);

	wb_control_undo_redo_truncate (wbc, 0, TRUE);
	tmp = g_slist_reverse (wb->undo_commands);
	for (ptr = tmp ; ptr != NULL ; ptr = ptr->next) {
		undo_label = get_menu_label (ptr);
		wb_control_undo_redo_push (wbc, TRUE, undo_label, ptr->data);
	}
	ignore = g_slist_reverse (tmp);	/* ignore, list is in undo_commands */

	wb_control_undo_redo_truncate (wbc, 0, FALSE);
	tmp = g_slist_reverse (wb->redo_commands);
	for (ptr = tmp ; ptr != NULL ; ptr = ptr->next) {
		redo_label = get_menu_label (ptr);
		wb_control_undo_redo_push (wbc, FALSE, redo_label, ptr->data);
	}
	ignore = g_slist_reverse (tmp);	/* ignore, list is in redo_commands */

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
	g_printerr ("Undo sizes:");
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
			g_printerr (" %d", size);
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
			g_printerr ("[trunc]\n");
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
	g_printerr ("\n");
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
	wb = wb_control_get_workbook (wbc);

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
	cmd->workbook_modified_before_do =
		go_doc_is_dirty (wb_control_get_doc (wbc));

	g_return_val_if_fail (cmd != NULL, TRUE);

	klass = CMD_CLASS (cmd);
	g_return_val_if_fail (klass != NULL, TRUE);

	/* TRUE indicates a failure to do the command */
	trouble = klass->redo_cmd (cmd, wbc);
	update_after_action (cmd->sheet, wbc);

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

	workbook_sheet_delete (sheet);

	return (TRUE);
}

/******************************************************************/

#define CMD_SET_TEXT_TYPE        (cmd_set_text_get_type ())
#define CMD_SET_TEXT(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_SET_TEXT_TYPE, CmdSetText))

typedef struct {
	GnmCommand cmd;

	GnmEvalPos pos;
	gchar *text;
	PangoAttrList *markup;
	gboolean has_user_format;
	GnmCellRegion *old_contents;
	ColRowIndexList *columns, *rows;
	ColRowStateGroup *old_widths, *old_heights;
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
		paste_target_init (&pt, me->cmd.sheet, &r, PASTE_CONTENTS | PASTE_FORMATS),
		GO_CMD_CONTEXT (wbc));

	if (me->old_widths) {
		colrow_restore_state_group (me->cmd.sheet, TRUE,
					    me->columns,
					    me->old_widths);
		me->old_widths = NULL;
		colrow_index_list_destroy (me->columns);
		me->columns = NULL;
	}

	if (me->old_heights) {
		colrow_restore_state_group (me->cmd.sheet, FALSE,
					    me->rows,
					    me->old_heights);
		me->old_heights = NULL;
		colrow_index_list_destroy (me->rows);
		me->rows = NULL;
	}

	select_range (me->cmd.sheet, &r, wbc);

	return FALSE;
}

static gboolean
cmd_set_text_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdSetText *me = CMD_SET_TEXT (cmd);
	GnmRange r;
	GnmExprTop const *texpr;
	GnmCell *cell = sheet_cell_fetch (me->pos.sheet,
					  me->pos.eval.col,
					  me->pos.eval.row);
	sheet_cell_set_text (cell, me->text, me->markup);
	texpr = cell->base.texpr;

	if (!me->has_user_format && texpr) {
		GnmEvalPos ep;
		GOFormat *sf = auto_style_format_suggest
			(texpr,
			 eval_pos_init_pos (&ep,
					    me->cmd.sheet,
					    &me->pos.eval));
		if (sf) {
			GnmStyle *new_style = gnm_style_new ();
			GnmRange r;

			gnm_style_set_format (new_style, sf);
			go_format_unref (sf);
			r.start = r.end = me->pos.eval;
			sheet_apply_style (me->cmd.sheet, &r, new_style);
		}
	}

	range_init_cellpos (&r, &me->pos.eval);
	if (texpr || !VALUE_IS_STRING (cell->value))
		colrow_autofit (me->cmd.sheet, &r, TRUE, TRUE,
				TRUE, FALSE,
				&me->columns, &me->old_widths);
	else
		colrow_autofit (me->cmd.sheet, &r, FALSE, FALSE,
				TRUE, FALSE,
				&me->rows, &me->old_heights);

	select_range (me->cmd.sheet, &r, wbc);

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
	colrow_index_list_destroy (me->columns);
	colrow_index_list_destroy (me->rows);
	colrow_state_group_destroy (me->old_widths);
	colrow_state_group_destroy (me->old_heights);
	gnm_command_finalize (cmd);
}


static gboolean
cb_gnm_pango_attr_list_equal (PangoAttribute *a, gpointer _sl)
{
	GSList **sl = _sl;
	*sl = g_slist_prepend (*sl, a);
	return FALSE;
}

/*
 * This is a bit of a hack.  It might claim a difference even when things
 * actually are equal.  But not the other way around.
 */
static gboolean
gnm_pango_attr_list_equal (PangoAttrList const *l1, PangoAttrList const *l2)
{
	if (l1 == l2)
		return TRUE;
	else if (l1 == NULL || l2 == NULL)
		return FALSE;
	else {
		gboolean res;
		GSList *sl1 = NULL, *sl2 = NULL;
		(void)pango_attr_list_filter ((PangoAttrList *)l1,
					      cb_gnm_pango_attr_list_equal,
					      &sl1);
		(void)pango_attr_list_filter ((PangoAttrList *)l2,
					      cb_gnm_pango_attr_list_equal,
					      &sl2);

		while (sl1 && sl2) {
			const PangoAttribute *a1 = sl1->data;
			const PangoAttribute *a2 = sl2->data;
			if (a1->start_index != a2->start_index ||
			    a1->end_index != a2->end_index ||
			    !pango_attribute_equal (a1, a2))
				break;
			sl1 = g_slist_delete_link (sl1, sl1);
			sl2 = g_slist_delete_link (sl2, sl2);
		}

		res = (sl1 == sl2);
		g_slist_free (sl1);
		g_slist_free (sl2);
		return res;
	}
}

gboolean
cmd_set_text (WorkbookControl *wbc,
	      Sheet *sheet, GnmCellPos const *pos,
	      char const *new_text,
	      PangoAttrList *markup)
{
	CmdSetText *me;
	gchar *text, *corrected_text;
	GnmCell const *cell;
	char *where;
	gboolean truncated;
	GnmRange r;
	gboolean same_text = FALSE, same_markup = FALSE;

	g_return_val_if_fail (IS_SHEET (sheet), TRUE);
	g_return_val_if_fail (new_text != NULL, TRUE);

	/* Ensure that we are not splitting up an array */
	cell = sheet_cell_get (sheet, pos->col, pos->row);
	if (gnm_cell_is_nonsingleton_array (cell)) {
		gnm_cmd_context_error_splits_array (GO_CMD_CONTEXT (wbc),
			_("Set Text"), NULL);
		return TRUE;
	}

	corrected_text = autocorrect_tool (new_text);

	if (go_pango_attr_list_is_empty (markup))
		markup = NULL;

	if (cell) {
		char *old_text = gnm_cell_get_entered_text (cell);
		same_text = strcmp (old_text, corrected_text) == 0;
		g_free (old_text);

		if (same_text && cell->value && VALUE_IS_STRING (cell->value)) {
			const GOFormat *fmt = VALUE_FMT (cell->value);
			if (fmt && go_format_is_markup (fmt)) {
				const PangoAttrList *old_markup =
					go_format_get_markup (fmt);
				same_markup = gnm_pango_attr_list_equal (old_markup, markup);
			}
		}
	}

	if (same_text && same_markup) {
		g_free (corrected_text);
		return TRUE;
	}

	me = g_object_new (CMD_SET_TEXT_TYPE, NULL);

	me->pos.sheet = sheet;
	me->pos.eval = *pos;
	me->text = corrected_text;
	if (NULL != (me->markup = markup))
		pango_attr_list_ref (me->markup);
	r.start = r.end = *pos;
	me->old_contents = clipboard_copy_range (sheet, &r);

	text = make_undo_text (corrected_text, &truncated);

	me->cmd.sheet = sheet;
	me->cmd.size = 1;
	where = undo_cell_pos_name (sheet, pos);

	me->cmd.cmd_descriptor =
		same_text
		? g_strdup_printf (_("Editing style in %s"), where)
		: g_strdup_printf (_("Typing \"%s%s\" in %s"),
				   text,
				   truncated ? "..." : "",
				   where);
	g_free (where);
	g_free (text);

	me->has_user_format = !go_format_is_general (
		gnm_style_get_format (sheet_style_get (sheet, pos->col, pos->row)));

	return command_push_undo (wbc, G_OBJECT (me));
}

/******************************************************************/

#define CMD_AREA_SET_TEXT_TYPE        (cmd_area_set_text_get_type ())
#define CMD_AREA_SET_TEXT(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_AREA_SET_TEXT_TYPE, CmdAreaSetText))

typedef struct {
	GnmCommand cmd;

	GnmParsePos   pp;
	char	  *text;
	gboolean   as_array;
	GSList	*old_contents;
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
	g_return_val_if_fail (me->old_contents != NULL, TRUE);

	for (ranges = me->selection; ranges != NULL ; ranges = ranges->next) {
		GnmRange const *r = ranges->data;
		GnmCellRegion * c;
		GnmPasteTarget pt;

		g_return_val_if_fail (me->old_contents != NULL, TRUE);

		c = me->old_contents->data;
		clipboard_paste_region (c,
			paste_target_init (&pt, me->cmd.sheet, r, PASTE_CONTENTS | PASTE_FORMATS),
			GO_CMD_CONTEXT (wbc));
		cellregion_unref (c);
		me->old_contents = g_slist_remove (me->old_contents, c);
	}
	g_return_val_if_fail (me->old_contents == NULL, TRUE);

	return FALSE;
}

static gboolean
cmd_area_set_text_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdAreaSetText *me = CMD_AREA_SET_TEXT (cmd);
	GnmExprTop const *texpr = NULL;
	GSList *l;
	GnmStyle *new_style = NULL;
	char const *expr_txt;

	g_return_val_if_fail (me != NULL, TRUE);

	/* Check for array subdivision */
	if (sheet_ranges_split_region (me->cmd.sheet, me->selection,
				       GO_CMD_CONTEXT (wbc), _("Set Text")))
		return TRUE;

	/* Check for locked cells */
	if (cmd_selection_is_locked_effective (me->cmd.sheet, me->selection,
					       wbc, _("Set Text")))
		return TRUE;

	expr_txt = gnm_expr_char_start_p (me->text);
	if (expr_txt != NULL)
		texpr = gnm_expr_parse_str_simple (expr_txt, &me->pp);
	if (me->as_array) {
		if (texpr == NULL)
			return TRUE;
	} else if (texpr != NULL) {
		GnmEvalPos ep;
		GOFormat *sf = auto_style_format_suggest (texpr,
			eval_pos_init_pos (&ep, me->cmd.sheet, &me->pp.eval));
		gnm_expr_top_unref (texpr);
		texpr = NULL;
		if (sf != NULL) {
			new_style = gnm_style_new ();
			gnm_style_set_format (new_style, sf);
			go_format_unref (sf);
		}
	}

	/* Everything is ok. Store previous contents and perform the operation */
	for (l = me->selection ; l != NULL ; l = l->next) {
		GnmRange const *r = l->data;
		me->old_contents = g_slist_prepend (me->old_contents,
			clipboard_copy_range (me->cmd.sheet, r));

		/* Queue depends of region as a block beforehand */
		sheet_region_queue_recalc (me->cmd.sheet, r);

		/* If there is an expression then this was an array */
		if (texpr != NULL) {
			gnm_cell_set_array_formula (me->cmd.sheet,
						r->start.col, r->start.row,
						r->end.col, r->end.row,
						texpr);
			sheet_region_queue_recalc (me->cmd.sheet, r);
		} else {
			sheet_range_set_text (&me->pp, r, me->text);
			if (new_style) {
				gnm_style_ref (new_style);
				sheet_apply_style (me->cmd.sheet, r, new_style);
			}
		}

		/* mark contents as dirty */
		sheet_flag_status_update_range (me->cmd.sheet, r);
		sheet_queue_respan (me->cmd.sheet, r->start.row, r->end.row);
	}
	me->old_contents = g_slist_reverse (me->old_contents);
	sheet_redraw_all (me->cmd.sheet, FALSE);

	if (new_style)
		gnm_style_unref (new_style);

	return FALSE;
}

static void
cmd_area_set_text_finalize (GObject *cmd)
{
	CmdAreaSetText *me = CMD_AREA_SET_TEXT (cmd);

	g_free (me->text);

	if (me->old_contents != NULL) {
		GSList *l;
		for (l = me->old_contents ; l != NULL ; l = g_slist_remove (l, l->data))
			cellregion_unref (l->data);
		me->old_contents = NULL;
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
	CmdAreaSetText *me;
	gchar *text;
	gboolean truncated;

	me = g_object_new (CMD_AREA_SET_TEXT_TYPE, NULL);

	me->text        = g_strdup (new_text);
	me->selection   = selection_get_ranges (sv, FALSE /* No intersection */);
	me->old_contents = NULL;

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

	text = make_undo_text (new_text, &truncated);

	me->cmd.sheet = me->pp.sheet;
	me->cmd.size = 1;
	me->cmd.cmd_descriptor =
		g_strdup_printf (_("Typing \"%s%s\""),
				 text,
				 truncated ? "..." : "");

	g_free (text);

	return command_push_undo (wbc, G_OBJECT (me));
}

gboolean
cmd_create_data_table (WorkbookControl *wbc, Sheet *sheet, GnmRange const *r,
		       char const *col_input, char const *row_input)
{
	CmdAreaSetText *me = g_object_new (CMD_AREA_SET_TEXT_TYPE, NULL);

	parse_pos_init (&me->pp, NULL, sheet, r->start.col, r->start.row);
	me->text         = g_strdup_printf ("=TABLE(%s,%s)", row_input, col_input);
	me->selection    = g_slist_prepend (NULL, range_dup (r));
	me->old_contents = NULL;
	me->as_array	 = TRUE;
	me->cmd.sheet = sheet;
	me->cmd.size = 1;
	me->cmd.cmd_descriptor =
		g_strdup_printf (_("Creating a Data Table in %s"),
			range_as_string (r));

	return command_push_undo (wbc, G_OBJECT (me));
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

	GOUndo          *undo;
} CmdInsDelColRow;

static void
cmd_ins_del_colrow_repeat (GnmCommand const *cmd, WorkbookControl *wbc)
{
	CmdInsDelColRow const *orig = (CmdInsDelColRow const *) cmd;
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	Sheet *sheet = sv_sheet (sv);
	GnmRange const *r = selection_first_range (sv,
		GO_CMD_CONTEXT (wbc), _("Ins/Del Column/Row"));

	if (r == NULL)
		return;

	if (orig->is_cols) {
		int count = range_width (r);
		if (orig->is_insert)
			cmd_insert_cols (wbc, sheet, r->start.col, count);
		else
			cmd_delete_cols (wbc, sheet, r->start.col, count);
	} else {
		int count = range_height (r);
		if (orig->is_insert)
			cmd_insert_rows (wbc, sheet, r->start.row, count);
		else
			cmd_delete_rows (wbc, sheet, r->start.row, count);
	}
}

MAKE_GNM_COMMAND (CmdInsDelColRow, cmd_ins_del_colrow, cmd_ins_del_colrow_repeat);

static gboolean
cmd_ins_del_colrow_undo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdInsDelColRow *me = CMD_INS_DEL_COLROW (cmd);

	if (me->undo) {
		go_undo_undo (me->undo);
		g_object_unref (me->undo);
		me->undo = NULL;
	}

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
	gboolean trouble;
	GOCmdContext *cc = GO_CMD_CONTEXT (wbc);
	int idx = me->index;
	int count = me->count;

	if (me->is_insert) {
		if (me->is_cols)
			trouble = sheet_insert_cols (me->sheet, idx, count,
						     &me->undo, cc);
		else
			trouble = sheet_insert_rows (me->sheet, idx, count,
						     &me->undo, cc);
	} else {
		if (me->is_cols)
			trouble = sheet_delete_cols (me->sheet, idx, count,
						     &me->undo, cc);
		else
			trouble = sheet_delete_rows (me->sheet, idx, count,
						     &me->undo, cc);
	}

	/* Ins/Del Row/Col re-ants things completely to account
	 * for the shift of col/rows. */
	if (!trouble && me->cutcopied != NULL && me->cut_copy_view != NULL) {
		if (me->is_cut) {
			GnmRange s = *me->cutcopied;
			int key = me->is_insert ? count : -count;
			int threshold = me->is_insert ? idx : idx + 1;

			/*
			 * Really only applies if the regions that are
			 * inserted/deleted are above the cut/copied region.
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

			gnm_app_clipboard_cut_copy (wbc, me->is_cut,
						    me->cut_copy_view,
						    &s, FALSE);
		} else
			gnm_app_clipboard_unant ();
	}

	return trouble;
}

static void
cmd_ins_del_colrow_finalize (GObject *cmd)
{
	CmdInsDelColRow *me = CMD_INS_DEL_COLROW (cmd);

	if (me->undo)
		g_object_unref (me->undo);

	g_free (me->cutcopied);

	sv_weak_unref (&(me->cut_copy_view));

	gnm_command_finalize (cmd);
}

static gboolean
cmd_ins_del_colrow (WorkbookControl *wbc,
		    Sheet *sheet,
		    gboolean is_cols, gboolean is_insert,
		    char const *descriptor, int index, int count)
{
	CmdInsDelColRow *me;
	int first, last;
	GnmRange r;

	g_return_val_if_fail (IS_SHEET (sheet), TRUE);
	g_return_val_if_fail (count > 0, TRUE);

	me = g_object_new (CMD_INS_DEL_COLROW_TYPE, NULL);

	me->sheet = sheet;
	me->is_cols = is_cols;
	me->is_insert = is_insert;
	me->index = index;
	me->count = count;

	/* Range that will get deleted. */
	first = me->is_insert
		? colrow_max (is_cols, sheet) - count
		: index;
	last = first + count - 1;
	(is_cols ? range_init_cols : range_init_rows) (&r, first, last);

	/* Check for array subdivision */
	if (sheet_range_splits_region (sheet, &r, NULL, GO_CMD_CONTEXT (wbc),
				       descriptor)) {
		g_object_unref (me);
		return TRUE;
	}

	/* Check for locks */
	if (cmd_cell_range_is_locked_effective (sheet, &r, wbc, descriptor)) {
		g_object_unref (me);
		return TRUE;
	}

	/* We store the cut or/copied range if applicable */
	if (!gnm_app_clipboard_is_empty () &&
	    gnm_app_clipboard_area_get () &&
	    sheet == gnm_app_clipboard_sheet_get ()) {
		me->cutcopied = range_dup (gnm_app_clipboard_area_get ());
		me->is_cut    = gnm_app_clipboard_is_cut ();
		sv_weak_ref (gnm_app_clipboard_sheet_view_get (),
			&(me->cut_copy_view));
	} else
		me->cutcopied = NULL;

	me->cmd.sheet = sheet;
	me->cmd.size = count * 10;  /* FIXME?  */
	me->cmd.cmd_descriptor = descriptor;

	return command_push_undo (wbc, G_OBJECT (me));
}

gboolean
cmd_insert_cols (WorkbookControl *wbc,
		 Sheet *sheet, int start_col, int count)
{
	/* g_strdup_printf does not support positional args, which screws the translators.
	 * We control the buffer contents so there is no worry of overflow
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
	 * We control the buffer contents so there is no worry of overflow
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
	GSList	  *old_contents;
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

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->selection != NULL, TRUE);
	g_return_val_if_fail (me->old_contents != NULL, TRUE);

	for (ranges = me->selection; ranges != NULL ; ranges = ranges->next) {
		GnmRange const *r = ranges->data;
		GnmCellRegion  *c;
		GnmPasteTarget pt;

		g_return_val_if_fail (me->old_contents != NULL, TRUE);

		c = me->old_contents->data;

		if (me->clear_flags)
			clipboard_paste_region (c,
				paste_target_init (&pt, me->cmd.sheet, r, me->paste_flags),
				GO_CMD_CONTEXT (wbc));

		cellregion_unref (c);
		me->old_contents = g_slist_remove (me->old_contents, c);
	}
	g_return_val_if_fail (me->old_contents == NULL, TRUE);

	select_selection (me->cmd.sheet, me->selection, wbc);

	return FALSE;
}

static gboolean
cmd_clear_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdClear *me = CMD_CLEAR (cmd);
	GSList *l;

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->selection != NULL, TRUE);
	g_return_val_if_fail (me->old_contents == NULL, TRUE);

	/* Check for array subdivision */
	if (sheet_ranges_split_region (me->cmd.sheet, me->selection,
				       GO_CMD_CONTEXT (wbc), _("Clear")))
		return TRUE;

	/* Check for locked cells */
	if (cmd_selection_is_locked_effective (me->cmd.sheet, me->selection, wbc, _("Clear")))
		return TRUE;

	for (l = me->selection ; l != NULL ; l = l->next) {
		GnmRange const *r = l->data;
		me->old_contents =
			g_slist_prepend (me->old_contents,
				clipboard_copy_range (me->cmd.sheet, r));

		/* We have already checked the arrays */
		sheet_clear_region (me->cmd.sheet,
			r->start.col, r->start.row, r->end.col, r->end.row,
			me->clear_flags|CLEAR_NOCHECKARRAY|CLEAR_RECALC_DEPS,
			GO_CMD_CONTEXT (wbc));
	}
	me->old_contents = g_slist_reverse (me->old_contents);

	select_selection (me->cmd.sheet, me->selection, wbc);

	return FALSE;
}

static void
cmd_clear_finalize (GObject *cmd)
{
	CmdClear *me = CMD_CLEAR (cmd);

	if (me->old_contents != NULL) {
		GSList *l;
		for (l = me->old_contents ; l != NULL ; l = g_slist_remove (l, l->data))
			cellregion_unref (l->data);
		me->old_contents = NULL;
	}
	range_fragment_free (me->selection);
	me->selection = NULL;

	gnm_command_finalize (cmd);
}

gboolean
cmd_selection_clear (WorkbookControl *wbc, int clear_flags)
{
	CmdClear *me;
	char *names;
	GString *types;
	int paste_flags;
	SheetView *sv = wb_control_cur_sheet_view (wbc);

	paste_flags = 0;
	if (clear_flags & CLEAR_VALUES)
		paste_flags |= PASTE_CONTENTS;
	if (clear_flags & CLEAR_FORMATS)
		paste_flags |= PASTE_FORMATS;
	if (clear_flags & CLEAR_COMMENTS)
		paste_flags |= PASTE_COMMENTS;

	me = g_object_new (CMD_CLEAR_TYPE, NULL);

	me->clear_flags = clear_flags;
	me->paste_flags = paste_flags;
	me->old_contents = NULL;
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
			GString *s = m->data;

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
	names = undo_range_list_name (me->cmd.sheet, me->selection);
	me->cmd.cmd_descriptor = g_strdup_printf (_("Clearing %s in %s"), types->str, names);

	g_free (names);
	g_string_free (types, TRUE);

	return command_push_undo (wbc, G_OBJECT (me));
}

/******************************************************************/

#define CMD_FORMAT_TYPE        (cmd_format_get_type ())
#define CMD_FORMAT(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_FORMAT_TYPE, CmdFormat))

typedef struct {
	GnmCellPos pos;
	GnmStyleList *styles;
	ColRowIndexList *rows;
	ColRowStateGroup *old_heights;
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

	if (orig->new_style)
		gnm_style_ref (orig->new_style);
	if (orig->borders)
		for (i = GNM_STYLE_BORDER_TOP; i < GNM_STYLE_BORDER_EDGE_MAX; i++)
			gnm_style_border_ref (orig->borders [i]);

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
		GSList *rstyles = g_slist_reverse (g_slist_copy (me->old_styles));
		GSList *rsel = g_slist_reverse (g_slist_copy (me->selection));
		GSList *l1, *l2;

		for (l1 = rstyles, l2 = rsel; l1; l1 = l1->next, l2 = l2->next) {
			CmdFormatOldStyle *os = l1->data;
			GnmRange const *r = l2->data;
			GnmSpanCalcFlags flags = sheet_style_set_list
				(me->cmd.sheet,
				 &os->pos, FALSE, os->styles);

			if (os->old_heights) {
				colrow_restore_state_group (me->cmd.sheet, FALSE,
							    os->rows,
							    os->old_heights);
				os->old_heights = NULL;
				colrow_index_list_destroy (os->rows);
				os->rows = NULL;
			}

			sheet_range_calc_spans (me->cmd.sheet, r, flags);
			sheet_flag_style_update_range (me->cmd.sheet, r);
		}

		sheet_redraw_all (me->cmd.sheet, FALSE);
		g_slist_free (rstyles);
		g_slist_free (rsel);
	}

	select_selection (me->cmd.sheet, me->selection, wbc);

	return FALSE;
}

static gboolean
cmd_format_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdFormat *me = CMD_FORMAT (cmd);
	GSList *l1, *l2;
	gboolean re_fit_height;

	g_return_val_if_fail (me != NULL, TRUE);

	/* Check for locked cells */
	if (cmd_selection_is_locked_effective (me->cmd.sheet, me->selection,
					       wbc, _("Changing Format")))
		return TRUE;

	re_fit_height =	me->new_style &&
		(GNM_SPANCALC_ROW_HEIGHT & gnm_style_required_spanflags (me->new_style));

	for (l1 = me->old_styles, l2 = me->selection; l2; l1 = l1->next, l2 = l2->next) {
		CmdFormatOldStyle *os = l1->data;
		GnmRange const *r = l2->data;

		if (me->borders)
			sheet_apply_border (me->cmd.sheet, r, me->borders);
		if (me->new_style) {
			gnm_style_ref (me->new_style);
			sheet_apply_style (me->cmd.sheet, r, me->new_style);
			if (re_fit_height)
				colrow_autofit (me->cmd.sheet, r, FALSE, FALSE,
						TRUE, FALSE,
						&os->rows, &os->old_heights);
		}

		sheet_flag_style_update_range (me->cmd.sheet, r);
	}
	sheet_redraw_all (me->cmd.sheet, FALSE);
	sheet_mark_dirty (me->cmd.sheet);

	select_selection (me->cmd.sheet, me->selection, wbc);

	return FALSE;
}

static void
cmd_format_finalize (GObject *cmd)
{
	CmdFormat *me = CMD_FORMAT (cmd);
	int        i;

	if (me->new_style)
		gnm_style_unref (me->new_style);
	me->new_style = NULL;

	if (me->borders) {
		for (i = GNM_STYLE_BORDER_TOP; i < GNM_STYLE_BORDER_EDGE_MAX; i++)
			gnm_style_border_unref (me->borders [i]);
		g_free (me->borders);
		me->borders = NULL;
	}

	if (me->old_styles != NULL) {
		GSList *l;

		for (l = me->old_styles ; l != NULL ; l = g_slist_remove (l, l->data)) {
			CmdFormatOldStyle *os = l->data;

			style_list_free (os->styles);
			colrow_index_list_destroy (os->rows);
			colrow_state_group_destroy (os->old_heights);
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
	CmdFormat *me;
	GSList    *l;
	SheetView *sv = wb_control_cur_sheet_view (wbc);

	me = g_object_new (CMD_FORMAT_TYPE, NULL);

	me->selection  = selection_get_ranges (sv, FALSE); /* TRUE ? */
	me->new_style  = style;

	me->cmd.sheet = sv_sheet (sv);
	me->cmd.size = 1;  /* Updated below.  */

	me->old_styles = NULL;
	for (l = me->selection; l; l = l->next) {
		GnmRange const *sel_r = l->data;
		GnmRange range = *sel_r;
		CmdFormatOldStyle *os;

		/* Store the containing range to handle borders */
		if (borders != NULL) {
			if (range.start.col > 0) range.start.col--;
			if (range.start.row > 0) range.start.row--;
			if (range.end.col < gnm_sheet_get_max_cols (me->cmd.sheet)-1) range.end.col++;
			if (range.end.row < gnm_sheet_get_max_rows (me->cmd.sheet)-1) range.end.row++;
		}

		os = g_new (CmdFormatOldStyle, 1);

		os->styles = sheet_style_get_list (me->cmd.sheet, &range);
		os->pos = range.start;
		os->rows = NULL;
		os->old_heights = NULL;

		me->cmd.size += g_slist_length (os->styles);
		me->old_styles = g_slist_append (me->old_styles, os);
	}

	if (borders) {
		int i;

		me->borders = g_new (GnmBorder *, GNM_STYLE_BORDER_EDGE_MAX);
		for (i = GNM_STYLE_BORDER_TOP; i < GNM_STYLE_BORDER_EDGE_MAX; i++)
			me->borders [i] = borders [i];
	} else
		me->borders = NULL;

	if (opt_translated_name == NULL) {
		char *names = undo_range_list_name (me->cmd.sheet, me->selection);

		me->cmd.cmd_descriptor = g_strdup_printf (_("Changing format of %s"), names);
		g_free (names);
	} else
		me->cmd.cmd_descriptor = g_strdup (opt_translated_name);

	return command_push_undo (wbc, G_OBJECT (me));
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

	colrow_index_list_destroy (me->selection);
	colrow_state_group_destroy (me->saved_sizes);

	gnm_command_finalize (cmd);
}

gboolean
cmd_resize_colrow (WorkbookControl *wbc, Sheet *sheet,
		   gboolean is_cols, ColRowIndexList *selection,
		   int new_size)
{
	CmdResizeColRow *me;
	GString *list;
	gboolean is_single;
	guint max_width;

	g_return_val_if_fail (IS_SHEET (sheet), TRUE);

	me = g_object_new (CMD_RESIZE_COLROW_TYPE, NULL);

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
	return command_push_undo (wbc, G_OBJECT (me));
}

/******************************************************************/

#define CMD_SORT_TYPE        (cmd_sort_get_type ())
#define CMD_SORT(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_SORT_TYPE, CmdSort))

typedef struct {
	GnmCommand cmd;

	GnmSortData *data;
	int         *perm;
	GnmCellRegion *old_contents;
} CmdSort;

MAKE_GNM_COMMAND (CmdSort, cmd_sort, NULL);

static void
cmd_sort_finalize (GObject *cmd)
{
	CmdSort *me = CMD_SORT (cmd);

	if (me->data != NULL)
		gnm_sort_data_destroy (me->data);
	g_free (me->perm);
	if (me->old_contents != NULL)
		cellregion_unref (me->old_contents);

	gnm_command_finalize (cmd);
}

static gboolean
cmd_sort_undo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdSort *me = CMD_SORT (cmd);
	GnmSortData *data = me->data;
	GnmPasteTarget pt;

	paste_target_init (&pt, data->sheet, data->range,
			   PASTE_CONTENTS | PASTE_FORMATS |
			   (data->retain_formats ? PASTE_FORMATS : 0));
	clipboard_paste_region (me->old_contents,
				&pt,
				GO_CMD_CONTEXT (wbc));

	return FALSE;
}

static gboolean
cmd_sort_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdSort *me = CMD_SORT (cmd);
	GnmSortData *data = me->data;

	/* Check for locks */
	if (cmd_cell_range_is_locked_effective
	    (data->sheet, data->range, wbc, _("Sorting")))
		return TRUE;

	if (me->perm)
		gnm_sort_position (data, me->perm, GO_CMD_CONTEXT (wbc));
	else {
		me->old_contents =
			clipboard_copy_range (data->sheet, data->range);
		me->cmd.size = cellregion_cmd_size (me->old_contents);
		me->perm = gnm_sort_contents (data, GO_CMD_CONTEXT (wbc));
	}

	return FALSE;
}

gboolean
cmd_sort (WorkbookControl *wbc, GnmSortData *data)
{
	CmdSort *me;
	char *desc;

	g_return_val_if_fail (data != NULL, TRUE);

	desc = g_strdup_printf (_("Sorting %s"), range_as_string (data->range));
	if (sheet_range_contains_region (data->sheet, data->range, GO_CMD_CONTEXT (wbc), desc)) {
		gnm_sort_data_destroy (data);
		g_free (desc);
		return TRUE;
	}

	me = g_object_new (CMD_SORT_TYPE, NULL);

	me->data = data;
	me->perm = NULL;
	me->cmd.sheet = data->sheet;
	me->cmd.size = 1;  /* Changed in initial redo.  */
	me->cmd.cmd_descriptor = desc;

	return command_push_undo (wbc, G_OBJECT (me));
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
			sv_selection_add_full (sv, y, x, y, 0,
					       y, gnm_sheet_get_max_rows (sheet) - 1);
		else
			sv_selection_add_full (sv, y, x, 0, x,
					       gnm_sheet_get_max_cols (sheet) - 1, x);
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
	CmdColRowHide *me;
	SheetView *sv = wb_control_cur_sheet_view (wbc);

	me = g_object_new (CMD_COLROW_HIDE_TYPE, NULL);

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

	return command_push_undo (wbc, G_OBJECT (me));
}

gboolean
cmd_selection_outline_change (WorkbookControl *wbc,
			      gboolean is_cols, int index, int depth)
{
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
		} else if (index+1 < colrow_max (is_cols, sheet)) {
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

	me = g_object_new (CMD_COLROW_HIDE_TYPE, NULL);

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

	return command_push_undo (wbc, G_OBJECT (me));
}

gboolean
cmd_global_outline_change (WorkbookControl *wbc, gboolean is_cols, int depth)
{
	CmdColRowHide *me;
	ColRowVisList *hide, *show;
	SheetView *sv	 = wb_control_cur_sheet_view (wbc);

	colrow_get_global_outline (sv_sheet (sv), is_cols, depth, &show, &hide);

	if (show == NULL && hide == NULL)
		return TRUE;

	me = g_object_new (CMD_COLROW_HIDE_TYPE, NULL);
	me->is_cols	= is_cols;
	me->hide	= hide;
	me->show	= show;
	me->cmd.sheet	= sv_sheet (sv);
	me->cmd.size = 1 + g_slist_length (me->show) + g_slist_length (me->hide);
	me->cmd.cmd_descriptor = g_strdup_printf (is_cols
		? _("Show column outline %d") : _("Show row outline %d"), depth);

	return command_push_undo (wbc, G_OBJECT (me));
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
	CmdGroup  *me;
	SheetView *sv;
	GnmRange	   r;

	g_return_val_if_fail (wbc != NULL, TRUE);

	sv = wb_control_cur_sheet_view (wbc);
	r = *selection_first_range (sv, NULL, NULL);

	/* Check if this really is possible and display an error if it's not */
	if (sheet_colrow_can_group (sv->sheet, &r, is_cols) != group) {
		if (group) {
			go_cmd_context_error_system (GO_CMD_CONTEXT (wbc), is_cols
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
			go_cmd_context_error_system (GO_CMD_CONTEXT (wbc), is_cols
					       ? _("Those columns are not grouped, you can't ungroup them")
					       : _("Those rows are not grouped, you can't ungroup them"));
			return TRUE;
		}
	}

	me = g_object_new (CMD_GROUP_TYPE, NULL);
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

	return command_push_undo (wbc, G_OBJECT (me));
}

/******************************************************************/

#define CMD_PASTE_CUT_TYPE        (cmd_paste_cut_get_type ())
#define CMD_PASTE_CUT(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_PASTE_CUT_TYPE, CmdPasteCut))

typedef struct {
	GnmCommand cmd;

	GnmExprRelocateInfo info;
	GSList *paste_contents;
	GOUndo *reloc_undo;
	gboolean move_selection;
	ColRowStateList *saved_sizes;

	/* handle redo-ing an undo with contents from a deleted sheet */
	GnmCellRegion *deleted_sheet_contents;
} CmdPasteCut;

MAKE_GNM_COMMAND (CmdPasteCut, cmd_paste_cut, NULL);

typedef struct {
	GnmPasteTarget pt;
	GnmCellRegion *contents;
} PasteContent;

/**
 * cmd_paste_cut_update :
 *
 * Utility routine to update things when we are transfering between sheets and
 * workbooks.
 */
static void
cmd_paste_cut_update (GnmExprRelocateInfo const *info,
		      G_GNUC_UNUSED WorkbookControl *wbc)
{
	Sheet *o = info->origin_sheet;
	Sheet *t = info->target_sheet;

	/* Dirty and update both sheets */
	sheet_mark_dirty (t);
	if (workbook_get_recalcmode (t->workbook))
		workbook_recalc (t->workbook);
	sheet_update (t);

	if (IS_SHEET (o) && o != t) {
		sheet_mark_dirty (o);
		if (o->workbook != t->workbook) {
			if (workbook_get_recalcmode (o->workbook))
				workbook_recalc (o->workbook);
		}
		sheet_update (o);
	}
}

static gboolean
cmd_paste_cut_undo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdPasteCut *me = CMD_PASTE_CUT (cmd);
	GnmExprRelocateInfo reverse;

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->paste_contents != NULL, TRUE);
	g_return_val_if_fail (me->deleted_sheet_contents == NULL, TRUE);

	reverse.reloc_type = GNM_EXPR_RELOCATE_MOVE_RANGE;
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
		sheet_move_range (&reverse, NULL, GO_CMD_CONTEXT (wbc));
	else
		me->deleted_sheet_contents = clipboard_copy_range (
			reverse.origin_sheet, &reverse.origin);

	/* Restore the original row heights */
	colrow_set_states (me->info.target_sheet, FALSE,
		reverse.origin.start.row, me->saved_sizes);
	colrow_state_list_destroy (me->saved_sizes);
	me->saved_sizes = NULL;

	if (me->reloc_undo) {
		go_undo_undo (me->reloc_undo);
		g_object_unref (me->reloc_undo);
		me->reloc_undo = NULL;
	}

	while (me->paste_contents) {
		PasteContent *pc = me->paste_contents->data;
		me->paste_contents = g_slist_remove (me->paste_contents, pc);

		clipboard_paste_region (pc->contents, &pc->pt, GO_CMD_CONTEXT (wbc));
		cellregion_unref (pc->contents);
		g_free (pc);
	}

	/* Force update of the status area */
	sheet_flag_status_update_range (me->info.target_sheet, NULL);

	cmd_paste_cut_update (&me->info, wbc);

	/* Select the original region */
	if (me->move_selection && IS_SHEET (me->info.origin_sheet))
		select_range (me->info.origin_sheet,
			      &me->info.origin,
			      wbc);

	return FALSE;
}

static gboolean
cmd_paste_cut_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdPasteCut *me = CMD_PASTE_CUT (cmd);
	GnmRange tmp;

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->paste_contents == NULL, TRUE);

	tmp = me->info.origin;
	range_translate (&tmp, me->info.col_offset, me->info.row_offset);
	range_normalize (&tmp);

	g_return_val_if_fail (range_is_sane (&tmp), TRUE);

	if (me->info.origin_sheet != me->info.target_sheet ||
	    !range_overlap (&me->info.origin, &tmp)) {
		PasteContent *pc = g_new (PasteContent, 1);
		paste_target_init (&pc->pt, me->info.target_sheet, &tmp, PASTE_ALL_TYPES);
		pc->contents = clipboard_copy_range (me->info.target_sheet, &tmp);
		me->paste_contents = g_slist_prepend (me->paste_contents, pc);
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
				me->paste_contents = g_slist_prepend (me->paste_contents, pc);
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
			GO_CMD_CONTEXT (wbc));
		clipboard_paste_region (me->deleted_sheet_contents,
			&pt, GO_CMD_CONTEXT (wbc));
		cellregion_unref (me->deleted_sheet_contents);
		me->deleted_sheet_contents = NULL;
	} else
		sheet_move_range (&me->info, &me->reloc_undo, GO_CMD_CONTEXT (wbc));

	cmd_paste_cut_update (&me->info, wbc);

	/* Backup row heights and adjust row heights to fit */
	me->saved_sizes = colrow_get_states (me->info.target_sheet, FALSE, tmp.start.row, tmp.end.row);
	rows_height_update (me->info.target_sheet, &tmp, FALSE);

	/* Make sure the destination is selected */
	if (me->move_selection)
		select_range (me->info.target_sheet, &tmp, wbc);

	return FALSE;
}

static void
cmd_paste_cut_finalize (GObject *cmd)
{
	CmdPasteCut *me = CMD_PASTE_CUT (cmd);

	if (me->saved_sizes)
		me->saved_sizes = colrow_state_list_destroy (me->saved_sizes);
	while (me->paste_contents) {
		PasteContent *pc = me->paste_contents->data;
		me->paste_contents = g_slist_remove (me->paste_contents, pc);
		cellregion_unref (pc->contents);
		g_free (pc);
	}
	if (me->reloc_undo) {
		g_object_unref (me->reloc_undo);
		me->reloc_undo = NULL;
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
	CmdPasteCut *me;
	GnmRange r;
	char *where;

	g_return_val_if_fail (info != NULL, TRUE);

	/* This is vacuous */
	if (info->origin_sheet == info->target_sheet &&
	    info->col_offset == 0 && info->row_offset == 0)
		return TRUE;

	/* FIXME: Do we want to show the destination range as well ? */
	where = undo_range_name (info->origin_sheet, &info->origin);
	if (descriptor == NULL)
		descriptor = g_strdup_printf (_("Moving %s"), where);
	g_free (where);

	g_return_val_if_fail (info != NULL, TRUE);

	r = info->origin;
	if (range_translate (&r, info->col_offset, info->row_offset)) {

		go_cmd_context_error_invalid (GO_CMD_CONTEXT (wbc), descriptor,
					_("is beyond sheet boundaries"));
		g_free (descriptor);
		return TRUE;
	}

	/* Check array subdivision & merged regions */
	if (sheet_range_splits_region (info->target_sheet, &r,
		(info->origin_sheet == info->target_sheet)
		? &info->origin : NULL, GO_CMD_CONTEXT (wbc), descriptor)) {
		g_free (descriptor);
		return TRUE;
	}

	me = g_object_new (CMD_PASTE_CUT_TYPE, NULL);

	me->info = *info;
	me->paste_contents  = NULL;
	me->deleted_sheet_contents = NULL;
	me->reloc_undo = NULL;
	me->move_selection = move_selection;
	me->saved_sizes    = NULL;

	me->cmd.sheet = NULL;  /* we have potentially two different.  */
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

	return command_push_undo (wbc, G_OBJECT (me));
}

/******************************************************************/

static void
warn_if_date_trouble (WorkbookControl *wbc, GnmCellRegion *cr)
{
	Workbook *wb = wb_control_get_workbook (wbc);
	const GODateConventions *wb_date_conv = workbook_date_conv (wb);

	if (cr->date_conv == NULL)
		return;
	if (go_date_conv_equal (cr->date_conv, wb_date_conv))
		return;

	/* We would like to show a warning, but it seems we cannot via a context.  */
	{
		GError *err;
		err = g_error_new (go_error_invalid(), 0,
				   _("Copying between files with different date conventions.\n"
				     "It is possible that some dates could be copied\n"
				     "incorrectly."));
		go_cmd_context_error (GO_CMD_CONTEXT (wbc), err);
		g_error_free (err);
	}
}


#define CMD_PASTE_COPY_TYPE        (cmd_paste_copy_get_type ())
#define CMD_PASTE_COPY(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_PASTE_COPY_TYPE, CmdPasteCopy))

typedef struct {
	GnmCommand cmd;

	GnmCellRegion   *contents;
	GSList          *pasted_objects,*orig_contents_objects;
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
		GO_CMD_CONTEXT (wbc), _("Paste Copy"));

	if (r == NULL)
		return;

	paste_target_init (&new_dst, sv_sheet (sv), r, orig->dst.paste_flags);
	cmd_paste_copy (wbc, &new_dst,
		clipboard_copy_range (orig->dst.sheet, &orig->dst.range));
}
MAKE_GNM_COMMAND (CmdPasteCopy, cmd_paste_copy, cmd_paste_copy_repeat);

static int
by_addr (gconstpointer a, gconstpointer b)
{
	if (GPOINTER_TO_UINT (a) < GPOINTER_TO_UINT (b))
		return -1;
	if (GPOINTER_TO_UINT (a) > GPOINTER_TO_UINT (b))
		return +1;
	return 0;
}

static GSList *
get_new_objects (Sheet *sheet, GSList *old)
{
	GSList *objs = g_slist_sort (g_slist_copy (sheet->sheet_objects),
				     by_addr);
	GSList *p = objs, *last = NULL;

	while (old) {
		int c = -1;
		while (p && (c = by_addr (p->data, old->data)) < 0) {
			last = p;
			p = p->next;
		}

		old = old->next;

		if (c == 0) {
			GSList *next = p->next;
			if (last)
				last->next = next;
			else
				objs = next;
			g_slist_free_1 (p);
			p = next;
		}
	}

	return objs;
}

static gboolean
cmd_paste_copy_impl (GnmCommand *cmd, WorkbookControl *wbc,
		     gboolean is_undo)
{
	CmdPasteCopy *me = CMD_PASTE_COPY (cmd);
	GnmCellRegion *contents;
	GSList *old_objects;

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->contents != NULL, TRUE);

	g_slist_foreach (me->pasted_objects,
			 (GFunc)sheet_object_clear_sheet,
			 NULL);
	go_slist_free_custom (me->pasted_objects, (GFreeFunc)g_object_unref);
	me->pasted_objects = NULL;
	old_objects = get_new_objects (me->dst.sheet, NULL);

	contents = clipboard_copy_range (me->dst.sheet, &me->dst.range);
	if (clipboard_paste_region (me->contents, &me->dst, GO_CMD_CONTEXT (wbc))) {
		/* There was a problem, avoid leaking */
		cellregion_unref (contents);
		g_slist_free (old_objects);
		return TRUE;
	}

	me->pasted_objects = get_new_objects (me->dst.sheet, old_objects);
	g_slist_foreach (me->pasted_objects, (GFunc)g_object_ref, NULL);
	g_slist_free (old_objects);

	if (me->has_been_through_cycle) {
		cellregion_unref (me->contents);
	} else {
		/* Save the contents */
		me->dst.paste_flags = PASTE_CONTENTS |
			(me->dst.paste_flags & PASTE_ALL_TYPES);
	}

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

	/*
	 * We cannot use the random set of objects at the target location.
	 * See http://bugzilla.gnome.org/show_bug.cgi?id=308300
	 */
	go_slist_free_custom (contents->objects, (GFreeFunc)g_object_unref);
	contents->objects = is_undo
		? go_slist_map (me->orig_contents_objects,
				(GOMapFunc)sheet_object_dup)
		: NULL;

	me->contents = contents;
	me->has_been_through_cycle = TRUE;

	/* Select the newly pasted contents  (this queues a redraw) */
	select_range (me->dst.sheet, &me->dst.range, wbc);

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
	if (me->contents) {
		if (me->has_been_through_cycle)
			cellregion_unref (me->contents);
		me->contents = NULL;
	}
	go_slist_free_custom (me->pasted_objects, (GFreeFunc)g_object_unref);
	go_slist_free_custom (me->orig_contents_objects, (GFreeFunc)g_object_unref);
	gnm_command_finalize (cmd);
}

gboolean
cmd_paste_copy (WorkbookControl *wbc,
		GnmPasteTarget const *pt, GnmCellRegion *cr)
{
	CmdPasteCopy *me;
	int n;
	char *range_name;

	g_return_val_if_fail (pt != NULL, TRUE);
	g_return_val_if_fail (IS_SHEET (pt->sheet), TRUE);

	me = g_object_new (CMD_PASTE_COPY_TYPE, NULL);

	me->cmd.sheet = pt->sheet;
	me->cmd.size = 1;  /* FIXME?  */

	range_name = undo_range_name (pt->sheet, &pt->range);
	me->cmd.cmd_descriptor = g_strdup_printf (_("Pasting into %s"),
						  range_name);
	g_free (range_name);

	me->dst = *pt;
	me->contents = cr;
	me->has_been_through_cycle = FALSE;
	me->saved_sizes = NULL;
	me->pasted_objects = NULL;
	me->orig_contents_objects =
		go_slist_map (cr->objects, (GOMapFunc)sheet_object_dup);

	/* If the input is only objects ignore all this range stuff */
	if (cr->cols < 1 || cr->rows < 1) {

	} else {	/* see if we need to do any tiling */
		GnmRange *r = &me->dst.range;
		if (pt->paste_flags & PASTE_TRANSPOSE) {
			n = range_width (r) / cr->rows;
			if (n < 1) n = 1;
			r->end.col = r->start.col + n * cr->rows - 1;

			n = range_height (r) / cr->cols;
			if (n < 1) n = 1;
			r->end.row = r->start.row + n * cr->cols - 1;
		} else {
			/* Before looking for tiling if we are not transposing,
			 * allow pasting a full col or row from a single cell */
			n = range_width (r);
			if (n == 1 && cr->cols == gnm_sheet_get_max_cols (me->cmd.sheet)) {
				r->start.col = 0;
				r->end.col = gnm_sheet_get_max_cols (me->cmd.sheet)-1;
			} else {
				n /= cr->cols;
				if (n < 1) n = 1;
				r->end.col = r->start.col + n * cr->cols - 1;
			}

			n = range_height (r);
			if (n == 1 && cr->rows == gnm_sheet_get_max_rows (me->cmd.sheet)) {
				r->start.row = 0;
				r->end.row = gnm_sheet_get_max_rows (me->cmd.sheet)-1;
			} else {
				n /= cr->rows;
				if (n < 1) n = 1;
				r->end.row = r->start.row + n * cr->rows - 1;
			}
		}

		if  (cr->cols != 1 || cr->rows != 1) {
			/* Note: when the source is a single cell, a single target merge is special */
			/* see clipboard.c (clipboard_paste_region)                                 */
			GnmRange const *merge = gnm_sheet_merge_is_corner (pt->sheet, &r->start);
			if (merge != NULL && range_equal (r, merge)) {
				/* destination is a single merge */
				/* enlarge it such that the source fits */
				if (pt->paste_flags & PASTE_TRANSPOSE) {
					if ((r->end.col - r->start.col + 1) < cr->rows)
						r->end.col = r->start.col + cr->rows - 1;
					if ((r->end.row - r->start.row + 1) < cr->cols)
						r->end.row = r->start.row + cr->cols - 1;
				} else {
					if ((r->end.col - r->start.col + 1) < cr->cols)
						r->end.col = r->start.col + cr->cols - 1;
					if ((r->end.row - r->start.row + 1) < cr->rows)
						r->end.row = r->start.row + cr->rows - 1;
				}
			}
		}
	}

	/* Use translate to do a quiet sanity check */
	if (range_translate (&me->dst.range, 0, 0)) {
		go_cmd_context_error_invalid (GO_CMD_CONTEXT (wbc),
					me->cmd.cmd_descriptor,
					_("is beyond sheet boundaries"));
		g_object_unref (me);
		return TRUE;
	}

	/* no need to test if all we have are objects */
	if (cr->cols > 0 && cr->rows > 0 &&
	    sheet_range_splits_region (pt->sheet, &me->dst.range,
				       NULL, GO_CMD_CONTEXT (wbc), me->cmd.cmd_descriptor)) {
		g_object_unref (me);
		return TRUE;
	}

	warn_if_date_trouble (wbc, cr);

	return command_push_undo (wbc, G_OBJECT (me));
}

/******************************************************************/

#define CMD_AUTOFILL_TYPE        (cmd_autofill_get_type ())
#define CMD_AUTOFILL(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_AUTOFILL_TYPE, CmdAutofill))

typedef struct {
	GnmCommand cmd;

	GnmCellRegion *contents;
	GnmPasteTarget dst;
	GnmRange src;
	int base_col, base_row, w, h, end_col, end_row;
	gboolean default_increment;
	gboolean inverse_autofill;
	ColRowIndexList *columns;
	ColRowStateGroup *old_widths;
} CmdAutofill;

static void
cmd_autofill_repeat (GnmCommand const *cmd, WorkbookControl *wbc)
{
	CmdAutofill const *orig = (CmdAutofill const *) cmd;
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	GnmRange const	*r = selection_first_range (sv,
		GO_CMD_CONTEXT (wbc), _("Autofill"));

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

	g_return_val_if_fail (wbc != NULL, TRUE);
	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->contents != NULL, TRUE);

	res = clipboard_paste_region (me->contents, &me->dst, GO_CMD_CONTEXT (wbc));
	cellregion_unref (me->contents);
	me->contents = NULL;

	if (me->old_widths) {
		colrow_restore_state_group (me->cmd.sheet, TRUE,
					    me->columns,
					    me->old_widths);
		me->old_widths = NULL;
		colrow_index_list_destroy (me->columns);
		me->columns = NULL;
	}

	if (res)
		return TRUE;

	select_range (me->dst.sheet, &me->src, wbc);

	return FALSE;
}

static gboolean
cmd_autofill_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdAutofill *me = CMD_AUTOFILL (cmd);
	GnmRange r;

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->contents == NULL, TRUE);

	me->contents = clipboard_copy_range (me->dst.sheet, &me->dst.range);

	g_return_val_if_fail (me->contents != NULL, TRUE);

	/* FIXME : when we split autofill to support hints and better validation
	 * move this in there.
	 */
	/* MW: May 2006: we support hints now.  What's this about?  */
	sheet_clear_region (me->dst.sheet,
		me->dst.range.start.col, me->dst.range.start.row,
		me->dst.range.end.col,   me->dst.range.end.row,
		CLEAR_VALUES | CLEAR_MERGES | CLEAR_NOCHECKARRAY | CLEAR_RECALC_DEPS,
		GO_CMD_CONTEXT (wbc));

	if (me->cmd.size == 1)
		me->cmd.size += cellregion_cmd_size (me->contents);
	if (me->inverse_autofill)
		gnm_autofill_fill (me->dst.sheet, me->default_increment,
			me->end_col, me->end_row, me->w, me->h,
			me->base_col, me->base_row);
	else
		gnm_autofill_fill (me->dst.sheet, me->default_increment,
			me->base_col, me->base_row, me->w, me->h,
			me->end_col, me->end_row);

	colrow_autofit (me->cmd.sheet, &me->dst.range, TRUE, TRUE,
			TRUE, FALSE,
			&me->columns, &me->old_widths);

	sheet_region_queue_recalc (me->dst.sheet, &me->dst.range);
	sheet_range_calc_spans (me->dst.sheet, &me->dst.range, GNM_SPANCALC_RENDER);
	sheet_flag_status_update_range (me->dst.sheet, &me->dst.range);

	r = range_union (&me->dst.range, &me->src);
	select_range (me->dst.sheet, &r, wbc);

	return FALSE;
}

static void
cmd_autofill_finalize (GObject *cmd)
{
	CmdAutofill *me = CMD_AUTOFILL (cmd);

	if (me->contents) {
		cellregion_unref (me->contents);
		me->contents = NULL;
	}
	colrow_index_list_destroy (me->columns);
	colrow_state_group_destroy (me->old_widths);
	gnm_command_finalize (cmd);
}

gboolean
cmd_autofill (WorkbookControl *wbc, Sheet *sheet,
	      gboolean default_increment,
	      int base_col, int base_row,
	      int w, int h, int end_col, int end_row,
	      gboolean inverse_autofill)
{
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
	if (sheet_range_splits_region (sheet, &target, NULL, GO_CMD_CONTEXT (wbc), _("Autofill")) ||
	    sheet_range_splits_region (sheet, &src, NULL, GO_CMD_CONTEXT (wbc), _("Autofill")))
		return TRUE;

	me = g_object_new (CMD_AUTOFILL_TYPE, NULL);

	me->contents = NULL;
	me->dst.sheet = sheet;
	me->dst.paste_flags = PASTE_CONTENTS | PASTE_FORMATS;
	me->dst.range = target;
	me->src = src;

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
		range_as_string (&me->dst.range));

	return command_push_undo (wbc, G_OBJECT (me));
}

/******************************************************************/

#define CMD_COPYREL_TYPE        (cmd_copyrel_get_type ())
#define CMD_COPYREL(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_COPYREL_TYPE, CmdCopyRel))

typedef struct {
	GnmCommand cmd;

	GOUndo *undo;
	GnmPasteTarget dst, src;
	int dx, dy;
	char const *name;
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

	g_return_val_if_fail (wbc != NULL, TRUE);
	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->undo != NULL, TRUE);

	go_undo_undo (me->undo);

	/* Select the newly pasted contents  (this queues a redraw) */
	select_range (me->dst.sheet, &me->dst.range, wbc);

	return FALSE;
}

static gboolean
cmd_copyrel_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdCopyRel *me = CMD_COPYREL (cmd);
	GnmCellRegion *contents;
	gboolean res;

	g_return_val_if_fail (me != NULL, TRUE);

	sheet_clear_region (me->dst.sheet,
		me->dst.range.start.col, me->dst.range.start.row,
		me->dst.range.end.col,   me->dst.range.end.row,
		CLEAR_VALUES | CLEAR_MERGES | CLEAR_NOCHECKARRAY | CLEAR_RECALC_DEPS,
		GO_CMD_CONTEXT (wbc));

	contents = clipboard_copy_range (me->src.sheet, &me->src.range);
	res = clipboard_paste_region (contents, &me->dst, GO_CMD_CONTEXT (wbc));
	cellregion_unref (contents);
	if (res)
		return TRUE;

	sheet_region_queue_recalc (me->dst.sheet, &me->dst.range);
	sheet_range_calc_spans (me->dst.sheet, &me->dst.range, GNM_SPANCALC_RENDER);
	sheet_flag_status_update_range (me->dst.sheet, &me->dst.range);

	/* Select the newly pasted contents  (this queues a redraw) */
	select_range (me->dst.sheet, &me->dst.range, wbc);

	return FALSE;
}

static void
cmd_copyrel_finalize (GObject *cmd)
{
	CmdCopyRel *me = CMD_COPYREL (cmd);

	if (me->undo)
		g_object_unref (me->undo);

	gnm_command_finalize (cmd);
}

gboolean
cmd_copyrel (WorkbookControl *wbc,
	     int dx, int dy,
	     char const *name)
{
	CmdCopyRel *me;
	GnmRange target, src;
	SheetView *sv  = wb_control_cur_sheet_view (wbc);
	Sheet *sheet = sv->sheet;
	GnmRange const *selr =
		selection_first_range (sv, GO_CMD_CONTEXT (wbc), name);

	g_return_val_if_fail (dx == 0 || dy == 0, TRUE);

	if (!selr)
		return FALSE;

	target = *selr;
	range_normalize (&target);
	src.start = src.end = target.start;

	if (dy) {
		src.end.col = target.end.col;
		if (target.start.row != target.end.row)
			target.start.row++;
		else
			src.start.row = src.end.row = (target.start.row + dy);
	}

	if (dx) {
		src.end.row = target.end.row;
		if (target.start.col != target.end.col)
			target.start.col++;
		else
			src.start.col = src.end.col = (target.start.col + dx);
	}

	if (src.start.col < 0 || src.start.col >= gnm_sheet_get_max_cols (sheet) ||
	    src.start.row < 0 || src.start.row >= gnm_sheet_get_max_rows (sheet))
		return FALSE;

	/* Check arrays or merged regions in src or target regions */
	if (sheet_range_splits_region (sheet, &target, NULL, GO_CMD_CONTEXT (wbc), name) ||
	    sheet_range_splits_region (sheet, &src, NULL, GO_CMD_CONTEXT (wbc), name))
		return TRUE;

	me = g_object_new (CMD_COPYREL_TYPE, NULL);

	me->dst.sheet = sheet;
	me->dst.paste_flags = PASTE_CONTENTS | PASTE_FORMATS;
	me->dst.range = target;
	me->src.sheet = sheet;
	me->src.paste_flags = PASTE_CONTENTS | PASTE_FORMATS;
	me->src.range = src;
	me->dx = dx;
	me->dy = dy;
	me->name = name;
	me->undo = clipboard_copy_range_undo (me->dst.sheet, &me->dst.range);

	me->cmd.sheet = sheet;
	me->cmd.size = 1;
	me->cmd.cmd_descriptor = g_strdup (name);

	return command_push_undo (wbc, G_OBJECT (me));
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

	GnmFormatTemplate *ft;    /* Template that has been applied */
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
			GnmSpanCalcFlags flags = sheet_style_set_list (me->cmd.sheet,
					    &os->pos, FALSE, os->styles);

			g_return_val_if_fail (l2 && l2->data, TRUE);

			r = l2->data;
			sheet_range_calc_spans (me->cmd.sheet, r, flags);
			if (flags != GNM_SPANCALC_SIMPLE)
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
cmd_selection_autoformat (WorkbookControl *wbc, GnmFormatTemplate *ft)
{
	CmdAutoFormat *me;
	char      *names;
	GSList    *l;
	SheetView *sv = wb_control_cur_sheet_view (wbc);

	me = g_object_new (CMD_AUTOFORMAT_TYPE, NULL);

	me->selection = selection_get_ranges (sv, FALSE); /* Regions may overlap */
	me->ft  = ft;
	me->cmd.sheet = sv_sheet (sv);
	me->cmd.size = 1;  /* FIXME?  */

	if (!format_template_check_valid (ft, me->selection, GO_CMD_CONTEXT (wbc))) {
		g_object_unref (me);
		return TRUE;
	}

	me->old_styles = NULL;
	for (l = me->selection; l; l = l->next) {
		CmdFormatOldStyle *os;
		GnmRange range = *((GnmRange const *) l->data);

		/* Store the containing range to handle borders */
		if (range.start.col > 0) range.start.col--;
		if (range.start.row > 0) range.start.row--;
		if (range.end.col < gnm_sheet_get_max_cols (sv->sheet)-1) range.end.col++;
		if (range.end.row < gnm_sheet_get_max_rows (sv->sheet)-1) range.end.row++;

		os = g_new (CmdFormatOldStyle, 1);

		os->styles = sheet_style_get_list (me->cmd.sheet, &range);
		os->pos = range.start;

		me->old_styles = g_slist_append (me->old_styles, os);
	}

	names = undo_range_list_name (me->cmd.sheet, me->selection);
	me->cmd.cmd_descriptor = g_strdup_printf (_("Autoformatting %s"),
						     names);
	g_free (names);

	return command_push_undo (wbc, G_OBJECT (me));
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
		gnm_sheet_merge_add (me->cmd.sheet, tmp, TRUE, GO_CMD_CONTEXT (wbc));
		sheet_range_calc_spans (me->cmd.sheet, tmp, GNM_SPANCALC_RE_RENDER);
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
		GSList *ptr, *merged = gnm_sheet_merge_get_overlap (me->cmd.sheet,
			&(g_array_index (me->ranges, GnmRange, i)));
		for (ptr = merged ; ptr != NULL ; ptr = ptr->next) {
			GnmRange const tmp = *(GnmRange *)(ptr->data);
			g_array_append_val (me->unmerged_regions, tmp);
			gnm_sheet_merge_remove (me->cmd.sheet, &tmp, GO_CMD_CONTEXT (wbc));
			sheet_range_calc_spans (me->cmd.sheet, &tmp,
						GNM_SPANCALC_RE_RENDER);
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
	CmdUnmergeCells *me;
	char *names;

	g_return_val_if_fail (IS_SHEET (sheet), TRUE);

	me = g_object_new (CMD_UNMERGE_CELLS_TYPE, NULL);

	me->cmd.sheet = sheet;
	me->cmd.size = 1;

	names = undo_range_list_name (sheet, selection);
	me->cmd.cmd_descriptor = g_strdup_printf (_("Unmerging %s"), names);
	g_free (names);

	me->unmerged_regions = NULL;
	me->ranges = g_array_new (FALSE, FALSE, sizeof (GnmRange));
	for ( ; selection != NULL ; selection = selection->next) {
		GSList *merged = gnm_sheet_merge_get_overlap (sheet, selection->data);
		if (merged != NULL) {
			g_array_append_val (me->ranges, *(GnmRange *)selection->data);
			g_slist_free (merged);
		}
	}

	if (me->ranges->len <= 0) {
		g_object_unref (me);
		return TRUE;
	}

	return command_push_undo (wbc, G_OBJECT (me));
}

/******************************************************************/

#define CMD_MERGE_CELLS_TYPE        (cmd_merge_cells_get_type ())
#define CMD_MERGE_CELLS(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_MERGE_CELLS_TYPE, CmdMergeCells))

typedef struct {
	GnmCommand cmd;
	GArray	*ranges;
	GSList	*old_contents;
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
		gnm_sheet_merge_remove (me->cmd.sheet, r, GO_CMD_CONTEXT (wbc));
	}

	/* Avoid pasting comments that are at 0,0.  Redo copies the target
	 * region (including all comments) .  If there was a comment in the top
	 * left we would end up duplicating it. */
	flags = PASTE_CONTENTS | PASTE_FORMATS | PASTE_COMMENTS |
		PASTE_IGNORE_COMMENTS_AT_ORIGIN;
	if (me->center)
		flags |= PASTE_FORMATS;
	for (i = 0 ; i < me->ranges->len ; ++i) {
		GnmRange const *r = &(g_array_index (me->ranges, GnmRange, i));
		GnmPasteTarget pt;
		GnmCellRegion * c;

		g_return_val_if_fail (me->old_contents != NULL, TRUE);

		c = me->old_contents->data;
		clipboard_paste_region (c,
			paste_target_init (&pt, me->cmd.sheet, r, flags),
			GO_CMD_CONTEXT (wbc));
		cellregion_unref (c);
		me->old_contents = g_slist_remove (me->old_contents, c);
	}
	g_return_val_if_fail (me->old_contents == NULL, TRUE);

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
		align_center = gnm_style_new ();
		gnm_style_set_align_h (align_center, HALIGN_CENTER);
	}
	sheet = me->cmd.sheet;
	for (i = 0 ; i < me->ranges->len ; ++i) {
		GnmRange const *r = &(g_array_index (me->ranges, GnmRange, i));
		GSList *ptr, *merged = gnm_sheet_merge_get_overlap (sheet, r);

		/* save contents before removing contained merged regions */
		me->old_contents = g_slist_prepend (me->old_contents,
			clipboard_copy_range (sheet, r));
		for (ptr = merged ; ptr != NULL ; ptr = ptr->next)
			gnm_sheet_merge_remove (sheet, ptr->data, GO_CMD_CONTEXT (wbc));
		g_slist_free (merged);

		gnm_sheet_merge_add (sheet, r, TRUE, GO_CMD_CONTEXT (wbc));
		if (me->center)
			sheet_apply_style (me->cmd.sheet, r, align_center);
	}

	if (me->center)
		gnm_style_unref (align_center);
	me->old_contents = g_slist_reverse (me->old_contents);
	return FALSE;
}

static void
cmd_merge_cells_finalize (GObject *cmd)
{
	CmdMergeCells *me = CMD_MERGE_CELLS (cmd);

	if (me->old_contents != NULL) {
		GSList *l;
		for (l = me->old_contents ; l != NULL ; l = g_slist_remove (l, l->data))
			cellregion_unref (l->data);
		me->old_contents = NULL;
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
	CmdMergeCells *me;
	char *names;

	g_return_val_if_fail (IS_SHEET (sheet), TRUE);

	me = g_object_new (CMD_MERGE_CELLS_TYPE, NULL);

	me->cmd.sheet = sheet;
	me->cmd.size = 1;

	names = undo_range_list_name (sheet, selection);
	me->cmd.cmd_descriptor =
		g_strdup_printf ((center ? _("Merge and Center %s") :_("Merging %s")), names);
	g_free (names);

	me->center = center;
	me->ranges = g_array_new (FALSE, FALSE, sizeof (GnmRange));
	for ( ; selection != NULL ; selection = selection->next) {
		GnmRange const *exist;
		GnmRange const *r = selection->data;
		if (range_is_singleton (selection->data))
			continue;
		if (NULL != (exist = gnm_sheet_merge_is_corner (sheet, &r->start)) &&
		    range_equal (r, exist))
			continue;
		g_array_append_val (me->ranges, *(GnmRange *)selection->data);
	}

	if (me->ranges->len <= 0) {
		g_object_unref (me);
		return TRUE;
	}

	return command_push_undo (wbc, G_OBJECT (me));
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
cmd_search_replace_update_after_action (CmdSearchReplace *me,
					WorkbookControl *wbc)
{
	GList *tmp;
	Sheet *last_sheet = NULL;

	for (tmp = me->cells; tmp; tmp = tmp->next) {
		SearchReplaceItem *sri = tmp->data;
		if (sri->pos.sheet != last_sheet) {
			last_sheet = sri->pos.sheet;
			update_after_action (last_sheet, wbc);
		}
	}
}


static gboolean
cmd_search_replace_undo (GnmCommand *cmd,
			 WorkbookControl *wbc)
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
				sheet_get_comment (sri->pos.sheet,
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
	cmd_search_replace_update_after_action (me, wbc);

	return FALSE;
}

static gboolean
cmd_search_replace_redo (GnmCommand *cmd,
			 WorkbookControl *wbc)
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
				sheet_get_comment (sri->pos.sheet,
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
	cmd_search_replace_update_after_action (me, wbc);

	return FALSE;
}

static gboolean
cmd_search_replace_do_cell (CmdSearchReplace *me, GnmEvalPos *ep,
			    gboolean test_run)
{
	GnmSearchReplace *sr = me->sr;

	GnmSearchReplaceCellResult cell_res;
	GnmSearchReplaceCommentResult comment_res;

	if (gnm_search_replace_cell (sr, ep, TRUE, &cell_res)) {
		GnmExprTop const *texpr;
		GnmValue *val;
		gboolean err;
		GnmParsePos pp;

		parse_pos_init_evalpos (&pp, ep);
		parse_text_value_or_expr (&pp, cell_res.new_text, &val, &texpr,
			gnm_style_get_format (gnm_cell_get_style (cell_res.cell)),
			workbook_date_conv (cell_res.cell->base.sheet->workbook));

		/*
		 * FIXME: this is a hack, but parse_text_value_or_expr
		 * does not have a better way of signaling an error.
		 */
		err = val && gnm_expr_char_start_p (cell_res.new_text);

		if (val) value_release (val);
		if (texpr) gnm_expr_top_unref (texpr);

		if (err) {
			if (test_run) {
				if (sr->query_func)
					sr->query_func (GNM_SRQ_FAIL,
							sr,
							cell_res.cell,
							cell_res.old_text,
							cell_res.new_text);
				g_free (cell_res.old_text);
				g_free (cell_res.new_text);
				return TRUE;
			} else {
				switch (sr->error_behaviour) {
				case GNM_SRE_ERROR: {
					GnmExprTop const *ee =
						gnm_expr_top_new
						(gnm_expr_new_funcall1
						 (gnm_func_lookup ("ERROR", NULL),
						  gnm_expr_new_constant
						  (value_new_string_nocopy (cell_res.new_text))));
					GnmConventionsOut out;

					out.accum = g_string_new ("=");
					out.pp = &pp;
					out.convs = gnm_conventions_default;
					gnm_expr_top_as_gstring (ee, &out);
					gnm_expr_top_unref (ee);
					cell_res.new_text = g_string_free (out.accum, FALSE);
					err = FALSE;
					break;
				}
				case GNM_SRE_STRING: {
					GString *s = g_string_new ("'");
					g_string_append (s, cell_res.new_text);
					g_free (cell_res.new_text);
					cell_res.new_text = g_string_free (s, FALSE);
					err = FALSE;
					break;
				}
				case GNM_SRE_FAIL:
					g_assert_not_reached ();
				case GNM_SRE_SKIP:
				default:
					; /* Nothing */
				}
			}
		}

		if (!err && !test_run) {
			gboolean doit = TRUE;
			if (sr->query && sr->query_func) {
				int res = sr->query_func (GNM_SRQ_QUERY,
							  sr,
							  cell_res.cell,
							  cell_res.old_text,
							  cell_res.new_text);
				if (res == GTK_RESPONSE_CANCEL) {
					g_free (cell_res.old_text);
					g_free (cell_res.new_text);
					return TRUE;
				}
				doit = (res == GTK_RESPONSE_YES);
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

	if (!test_run && gnm_search_replace_comment (sr, ep, TRUE, &comment_res)) {
		gboolean doit = TRUE;

		if (sr->query && sr->query_func) {
			int res = sr->query_func (GNM_SRQ_QUERY_COMMENT,
						  sr,
						  ep->sheet,
						  &ep->eval,
						  comment_res.old_text,
						  comment_res.new_text);
			if (res == GTK_RESPONSE_CANCEL) {
				g_free (comment_res.new_text);
				return TRUE;
			}
			doit = (res == GTK_RESPONSE_YES);
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
cmd_search_replace_do (CmdSearchReplace *me, gboolean test_run,
		       WorkbookControl *wbc)
{
	GnmSearchReplace *sr = me->sr;
	GPtrArray *cells;
	gboolean result = FALSE;
	unsigned i;

	if (test_run) {
		switch (sr->error_behaviour) {
		case GNM_SRE_SKIP:
		case GNM_SRE_QUERY:
		case GNM_SRE_ERROR:
		case GNM_SRE_STRING:
			/* An error is not a problem.  */
			return FALSE;

		case GNM_SRE_FAIL:
			; /* Nothing.  */
		}
	}

	cells = gnm_search_collect_cells (sr);

	for (i = 0; i < cells->len; i++) {
		GnmEvalPos *ep = g_ptr_array_index (cells, i);

		if (cmd_search_replace_do_cell (me, ep, test_run)) {
			result = TRUE;
			break;
		}
	}

	gnm_search_collect_cells_free (cells);

	if (!test_run) {
		/* Cells were added in the wrong order.  Correct.  */
		me->cells = g_list_reverse (me->cells);

		cmd_search_replace_update_after_action (me, wbc);
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
	g_object_unref (me->sr);

	gnm_command_finalize (cmd);
}

gboolean
cmd_search_replace (WorkbookControl *wbc, GnmSearchReplace *sr)
{
	CmdSearchReplace *me;

	g_return_val_if_fail (sr != NULL, TRUE);

	me = g_object_new (CMD_SEARCH_REPLACE_TYPE, NULL);

	me->cells = NULL;
	me->sr = g_object_ref (sr);

	me->cmd.sheet = NULL;
	me->cmd.size = 1;  /* Corrected below. */
	me->cmd.cmd_descriptor = g_strdup (_("Search and Replace"));

	if (cmd_search_replace_do (me, TRUE, wbc)) {
		/* There was an error and nothing was done.  */
		g_object_unref (me);
		return TRUE;
	}

	cmd_search_replace_do (me, FALSE, wbc);
	me->cmd.size += g_list_length (me->cells);

	command_register_undo (wbc, G_OBJECT (me));
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
	CmdColRowStdSize *me;

	g_return_val_if_fail (IS_SHEET (sheet), TRUE);

	me = g_object_new (CMD_COLROW_STD_SIZE_TYPE, NULL);

	me->sheet = sheet;
	me->is_cols = is_cols;
	me->new_default = new_default;
	me->old_default = 0;

	me->cmd.sheet = sheet;
	me->cmd.size = 1;  /* Changed in initial redo.  */
	me->cmd.cmd_descriptor = is_cols
		? g_strdup_printf (_("Setting default width of columns to %.2fpts"), new_default)
		: g_strdup_printf (_("Setting default height of rows to %.2fpts"), new_default);

	return command_push_undo (wbc, G_OBJECT (me));
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
		g_object_set (sheet, "zoom-factor", me->old_factors[i], NULL);
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
		g_object_set (sheet, "zoom-factor", me->new_factor, NULL);
	}

	return FALSE;
}

static void
cmd_zoom_finalize (GObject *cmd)
{
	CmdZoom *me = CMD_ZOOM (cmd);

	g_slist_free (me->sheets);
	g_free (me->old_factors);

	gnm_command_finalize (cmd);
}

gboolean
cmd_zoom (WorkbookControl *wbc, GSList *sheets, double factor)
{
	CmdZoom *me;
	GString *namelist;
	GSList *l;
	int i;
	guint max_width;

	g_return_val_if_fail (wbc != NULL, TRUE);
	g_return_val_if_fail (sheets != NULL, TRUE);

	me = g_object_new (CMD_ZOOM_TYPE, NULL);

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

	return command_push_undo (wbc, G_OBJECT (me));
}

/******************************************************************/

#define CMD_OBJECTS_DELETE_TYPE (cmd_objects_delete_get_type ())
#define CMD_OBJECTS_DELETE(o)   (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_OBJECTS_DELETE_TYPE, CmdObjectsDelete))

typedef struct {
	GnmCommand cmd;
	GSList *objects;
	GArray *location;
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

static void
cmd_objects_restore_location (SheetObject *so, gint location)
{
	gint loc = sheet_object_get_stacking (so);
	if (loc != location)
		sheet_object_adjust_stacking(so, location - loc);
}

static gboolean
cmd_objects_delete_undo (GnmCommand *cmd,
			G_GNUC_UNUSED WorkbookControl *wbc)
{
	CmdObjectsDelete *me = CMD_OBJECTS_DELETE (cmd);
	GSList *l;
	gint i;

	g_slist_foreach (me->objects,
		(GFunc) sheet_object_set_sheet, me->cmd.sheet);

	for (l = me->objects, i = 0; l; l = l->next, i++)
		cmd_objects_restore_location (SHEET_OBJECT (l->data),
					      g_array_index(me->location,
							    gint, i));
	return FALSE;
}

static void
cmd_objects_delete_finalize (GObject *cmd)
{
	CmdObjectsDelete *me = CMD_OBJECTS_DELETE (cmd);
	go_slist_free_custom (me->objects, g_object_unref);
	if (me->location) {
		g_array_free (me->location, TRUE);
		me->location = NULL;
	}
	gnm_command_finalize (cmd);
}

static void
cmd_objects_store_location (SheetObject *so, GArray *location)
{
	gint loc = sheet_object_get_stacking (so);
	g_array_append_val (location, loc);
}

/* Absorbs the list, adding references to the contents */
gboolean
cmd_objects_delete (WorkbookControl *wbc, GSList *objects,
		    char const *name)
{
	CmdObjectsDelete *me;

	g_return_val_if_fail (IS_WORKBOOK_CONTROL (wbc), TRUE);
	g_return_val_if_fail (objects != NULL, TRUE);

	me = g_object_new (CMD_OBJECTS_DELETE_TYPE, NULL);

	me->objects = objects;
	g_slist_foreach (me->objects, (GFunc) g_object_ref, NULL);

	me->location = g_array_new (FALSE, FALSE, sizeof (gint));
	g_slist_foreach (me->objects, (GFunc) cmd_objects_store_location,
			 me->location);

	me->cmd.sheet = sheet_object_get_sheet (objects->data);
	me->cmd.size = 1;
	me->cmd.cmd_descriptor = g_strdup (name ? name : _("Delete Object"));

	return command_push_undo (wbc, G_OBJECT (me));
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
		sheet_object_anchor_assign (&tmp, sheet_object_get_anchor (obj->data));
		sheet_object_set_anchor	(obj->data, anch->data);
		sheet_object_anchor_assign (anch->data, &tmp);
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
	go_slist_free_custom (me->objects, g_object_unref);
	go_slist_free_custom (me->anchors, g_free);
	gnm_command_finalize (cmd);
}

gboolean
cmd_objects_move (WorkbookControl *wbc, GSList *objects, GSList *anchors,
		  gboolean objects_created, char const *name)
{
	CmdObjectsMove *me;

	g_return_val_if_fail (IS_WORKBOOK_CONTROL (wbc), TRUE);
	g_return_val_if_fail (NULL != objects, TRUE);
	g_return_val_if_fail (NULL != anchors, TRUE);
	g_return_val_if_fail (g_slist_length (objects) == g_slist_length (anchors), TRUE);

	/*
	 * There is no need to move the object around, because this has
	 * already happened.
	 */

	me = g_object_new (CMD_OBJECTS_MOVE_TYPE, NULL);

	me->first_time = TRUE;
	me->objects_created  = objects_created;
	me->objects = objects;
	g_slist_foreach (me->objects, (GFunc) g_object_ref, NULL);
	me->anchors = anchors;

	me->cmd.sheet = sheet_object_get_sheet (objects->data);
	me->cmd.size = 1;
	me->cmd.cmd_descriptor = g_strdup (name);

	return command_push_undo (wbc, G_OBJECT (me));
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
	sheet_mark_dirty (me->cmd.sheet);
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
	CmdObjectFormat *me;

	g_return_val_if_fail (IS_WORKBOOK_CONTROL (wbc), TRUE);
	g_return_val_if_fail (IS_SHEET_OBJECT (so), TRUE);

	me = g_object_new (CMD_OBJECT_FORMAT_TYPE, NULL);

	me->so    = g_object_ref (G_OBJECT (so));
	me->style = g_object_ref (G_OBJECT (orig_style));
	me->first_time = TRUE;

	me->cmd.sheet = sheet_object_get_sheet (so);
	me->cmd.size = 1;
	me->cmd.cmd_descriptor = g_strdup (_("Format Object"));

	return command_push_undo (wbc, G_OBJECT (me));
}

/******************************************************************/

#define CMD_REORGANIZE_SHEETS_TYPE        (cmd_reorganize_sheets_get_type ())
#define CMD_REORGANIZE_SHEETS(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_REORGANIZE_SHEETS_TYPE, CmdReorganizeSheets))

typedef struct {
	GnmCommand cmd;
	Workbook *wb;
	WorkbookSheetState *old;
	WorkbookSheetState *new;
	gboolean first;
	Sheet *undo_sheet;
	Sheet *redo_sheet;
} CmdReorganizeSheets;

MAKE_GNM_COMMAND (CmdReorganizeSheets, cmd_reorganize_sheets, NULL);

static gboolean
cmd_reorganize_sheets_undo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdReorganizeSheets *me = CMD_REORGANIZE_SHEETS (cmd);
	workbook_sheet_state_restore (me->wb, me->old);
	if (me->undo_sheet) {
		WORKBOOK_VIEW_FOREACH_CONTROL (wb_control_view (wbc), control,
			  wb_control_sheet_focus (control, me->undo_sheet););
	}
	return FALSE;
}

static gboolean
cmd_reorganize_sheets_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdReorganizeSheets *me = CMD_REORGANIZE_SHEETS (cmd);

	if (me->first)
		me->first = FALSE;
	else {
		workbook_sheet_state_restore (me->wb, me->new);
		if (me->redo_sheet) {
			WORKBOOK_VIEW_FOREACH_CONTROL (wb_control_view (wbc), control,
						       wb_control_sheet_focus (control, me->redo_sheet););
		}
	}

	return FALSE;
}

static void
cmd_reorganize_sheets_finalize (GObject *cmd)
{
	CmdReorganizeSheets *me = CMD_REORGANIZE_SHEETS (cmd);

	if (me->old)
		workbook_sheet_state_free (me->old);
	if (me->new)
		workbook_sheet_state_free (me->new);

	gnm_command_finalize (cmd);
}

gboolean
cmd_reorganize_sheets (WorkbookControl *wbc,
		       WorkbookSheetState *old_state,
		       Sheet *undo_sheet)
{
	CmdReorganizeSheets *me;
	Workbook *wb = wb_control_get_workbook (wbc);

	me = g_object_new (CMD_REORGANIZE_SHEETS_TYPE, NULL);
	me->wb = wb;
	me->old = old_state;
	me->new = workbook_sheet_state_new (me->wb);
	me->first = TRUE;
	me->undo_sheet = undo_sheet;
	me->redo_sheet = wb_control_cur_sheet (wbc);

	me->cmd.sheet = NULL;
	me->cmd.size = 1;
	me->cmd.cmd_descriptor =
		workbook_sheet_state_diff (me->old, me->new);

	if (me->cmd.cmd_descriptor)
		return command_push_undo (wbc, G_OBJECT (me));

	/* No change.  */
	g_object_unref (me);
	return FALSE;
}

/******************************************************************/

gboolean
cmd_rename_sheet (WorkbookControl *wbc,
		  Sheet *sheet,
		  char const *new_name)
{
	WorkbookSheetState *old_state;
	Sheet *collision;

	g_return_val_if_fail (new_name != NULL, TRUE);
	g_return_val_if_fail (sheet != NULL, TRUE);

	if (*new_name == 0) {
		go_cmd_context_error_invalid (GO_CMD_CONTEXT (wbc), _("Name"), _("Sheet names must be non-empty."));
		return TRUE;
	}

	collision = workbook_sheet_by_name (sheet->workbook, new_name);
	if (collision && collision != sheet) {
		GError *err = g_error_new (go_error_invalid(), 0,
					   _("A workbook cannot have two sheets with the same name."));
		go_cmd_context_error (GO_CMD_CONTEXT (wbc), err);
		g_error_free (err);
		return TRUE;
	}

	old_state = workbook_sheet_state_new (sheet->workbook);
	g_object_set (sheet, "name", new_name, NULL);
	return cmd_reorganize_sheets (wbc, old_state, sheet);
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

	comment = sheet_get_comment (sheet, pos);
	if (comment) {
		if (text)
			cell_comment_text_set (comment, text);
		else {
			GnmRange const *mr;

			mr = gnm_sheet_merge_contains_pos (sheet, pos);

			if (mr)
				sheet_objects_clear (sheet, mr, CELL_COMMENT_TYPE, NULL);
			else {
				GnmRange r;
				r.start = r.end = *pos;
				sheet_objects_clear (sheet, &r, CELL_COMMENT_TYPE, NULL);
			}
		}
	} else if (text && (strlen (text) > 0))
		cell_set_comment (sheet, pos, NULL, text);

	sheet_mark_dirty (sheet);
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
	CmdSetComment *me;
	GnmComment   *comment;
	char *where;

	g_return_val_if_fail (IS_SHEET (sheet), TRUE);
	g_return_val_if_fail (new_text != NULL, TRUE);

	me = g_object_new (CMD_SET_COMMENT_TYPE, NULL);

	me->cmd.sheet = sheet;
	me->cmd.size = 1;
	if (strlen (new_text) < 1)
		me->new_text = NULL;
	else
		me->new_text    = g_strdup (new_text);
	where = undo_cell_pos_name (sheet, pos);
	me->cmd.cmd_descriptor =
		g_strdup_printf (me->new_text == NULL ?
				 _("Clearing comment of %s") :
				 _("Setting comment of %s"),
				 where);
	g_free (where);
	me->old_text    = NULL;
	me->pos         = *pos;
	me->sheet       = sheet;
	comment = sheet_get_comment (sheet, pos);
	if (comment)
		me->old_text = g_strdup (cell_comment_text_get (comment));

	/* Register the command object */
	return command_push_undo (wbc, G_OBJECT (me));
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
	GnmCellRegion              *old_contents;
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
				    GO_CMD_CONTEXT (wbc));
		clipboard_paste_region (me->old_contents,
			paste_target_init (&pt, me->dao->sheet, &me->old_range, PASTE_ALL_TYPES),
			GO_CMD_CONTEXT (wbc));
		cellregion_unref (me->old_contents);
		me->old_contents = NULL;
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
		me->old_contents = NULL;
		break;
	case NewWorkbookOutput:
		/* No undo in this case (see below) */
		me->old_contents = NULL;
		break;
	case RangeOutput:
	default:
		range_init (&me->old_range, me->dao->start_col, me->dao->start_row,
			    me->dao->start_col + me->dao->cols - 1,
			    me->dao->start_row + me->dao->rows - 1);
		me->old_contents = clipboard_copy_range (me->dao->sheet, &me->old_range);
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
	sheet_mark_dirty (me->dao->sheet);
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
	if (me->old_contents)
		cellregion_unref (me->old_contents);

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
	CmdAnalysis_Tool *me;
	gboolean trouble;

	g_return_val_if_fail (dao != NULL, TRUE);
	g_return_val_if_fail (specs != NULL, TRUE);
	g_return_val_if_fail (engine != NULL, TRUE);

	me = g_object_new (CMD_ANALYSIS_TOOL_TYPE, NULL);

	dao->wbc = wbc;

	/* Store the specs for the object */
	me->specs = specs;
	me->specs_owned = FALSE;
	me->dao = dao;
	me->engine = engine;
	me->cmd.cmd_descriptor = NULL;
	if (me->engine (me->dao, me->specs, TOOL_ENGINE_UPDATE_DAO, NULL)) {
		g_object_unref (me);
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
	trouble = command_push_undo (wbc, G_OBJECT (me));

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
	GnmCellRegion *merge_contents;
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
	merge_contents = clipboard_copy_range (source_sheet, &target_range);
	state_col = colrow_get_states (source_sheet, TRUE, target_range.start.col,
					   target_range.end.col);
	state_row = colrow_get_states (source_sheet, FALSE, target_range.start.row,
					   target_range.end.row);

	for (i = 0; i < me->n; i++) {
		Sheet *new_sheet;

		new_sheet = workbook_sheet_add (me->sheet->workbook, -1);
		me->sheet_list = g_slist_prepend (me->sheet_list, new_sheet);

		colrow_set_states (new_sheet, TRUE, target_range.start.col, state_col);
		colrow_set_states (new_sheet, FALSE, target_range.start.row, state_row);
		sheet_objects_dup (source_sheet, new_sheet, &target_range);
		clipboard_paste_region (merge_contents,
			paste_target_init (&pt, new_sheet, &target_range, PASTE_ALL_TYPES),
			GO_CMD_CONTEXT (wbc));
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
					gnm_cell_assign_value (target_cell,
							   value_new_empty ());
			} else {
				GnmCell *target_cell = sheet_cell_fetch ((Sheet *)target_sheet->data,
								      col_target, row_target);
				gnm_cell_assign_value (target_cell,
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
	CmdMergeData *me;
	GnmRangeRef *cell;

	g_return_val_if_fail (IS_SHEET (sheet), TRUE);
	g_return_val_if_fail (merge_zone != NULL, TRUE);
	g_return_val_if_fail (merge_fields != NULL, TRUE);
	g_return_val_if_fail (merge_data != NULL, TRUE);

	me = g_object_new (CMD_MERGE_DATA_TYPE, NULL);

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
	return command_push_undo (wbc, G_OBJECT (me));
}

/******************************************************************/

#define CMD_CHANGE_META_DATA_TYPE        (cmd_change_summary_get_type ())
#define CMD_CHANGE_META_DATA(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_CHANGE_META_DATA_TYPE, CmdChangeMetaData))

typedef struct {
	GnmCommand cmd;
	GSList *changed_props;
	GSList *removed_names;
} CmdChangeMetaData;

MAKE_GNM_COMMAND (CmdChangeMetaData, cmd_change_summary, NULL);

static gboolean
cmd_change_summary_undo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdChangeMetaData *me = CMD_CHANGE_META_DATA (cmd);
	GsfDocMetaData *meta = go_doc_get_meta_data (wb_control_get_doc (wbc));
	GSList *ptr, *old_vals = NULL, *dropped = NULL;
	GsfDocProp *prop;
	char const *name;

	for (ptr = me->removed_names; ptr != NULL ; ptr = ptr->next) {
		if (NULL != (prop = gsf_doc_meta_data_steal (meta, ptr->data)))
			old_vals = g_slist_prepend (old_vals, prop);
		g_free (ptr->data);
	}
	g_slist_free (me->removed_names);

	for (ptr = me->changed_props; ptr != NULL ; ptr = ptr->next) {
		name = gsf_doc_prop_get_name (ptr->data);
		if (NULL != (prop = gsf_doc_meta_data_steal (meta, name)))
			old_vals = g_slist_prepend (old_vals, prop);
		else
			dropped = g_slist_prepend (old_vals, g_strdup (name));
		gsf_doc_meta_data_store (meta, ptr->data);
	}
	g_slist_free (me->changed_props);

	me->removed_names = dropped;
	me->changed_props = old_vals;
	go_doc_update_meta_data (wb_control_get_doc (wbc));

	return FALSE;
}

static gboolean
cmd_change_summary_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	return cmd_change_summary_undo (cmd, wbc);
}

static void
cmd_change_summary_finalize (GObject *cmd)
{
	CmdChangeMetaData *me = CMD_CHANGE_META_DATA (cmd);

	go_slist_free_custom (me->changed_props, (GFreeFunc)gsf_doc_prop_free);
	me->changed_props = NULL;
	go_slist_free_custom (me->removed_names, g_free);
	me->removed_names = NULL;

	gnm_command_finalize (cmd);
}

gboolean
cmd_change_meta_data (WorkbookControl *wbc, GSList *changes, GSList *removed)
{
	CmdChangeMetaData *me = g_object_new (CMD_CHANGE_META_DATA_TYPE, NULL);

	me->changed_props = changes;
	me->removed_names = removed;
	me->cmd.sheet = NULL;

	me->cmd.size = g_slist_length (changes) + g_slist_length (removed);
	me->cmd.cmd_descriptor = g_strdup_printf (
		_("Changing workbook properties"));
	return command_push_undo (wbc, G_OBJECT (me));
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
		me->changed_positions = sheet_object_adjust_stacking (me->so, G_MAXINT/2);
		break;
	case cmd_object_pull_forward:
		me->changed_positions = sheet_object_adjust_stacking (me->so, 1);
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
	g_object_unref (me->so);
	gnm_command_finalize (cmd);
}

gboolean
cmd_object_raise (WorkbookControl *wbc, SheetObject *so, CmdObjectRaiseSelector dir)
{
	CmdObjectRaise *me;

	g_return_val_if_fail (IS_SHEET_OBJECT (so), TRUE);

	me = g_object_new (CMD_OBJECT_RAISE_TYPE, NULL);

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

	return command_push_undo (wbc, G_OBJECT (me));
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
		book = wb_control_get_workbook(wbc);
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
		book = wb_control_get_workbook(wbc);
		n = workbook_sheet_count (book);
		for (i = 0 ; i < n ; i++) {
			Sheet * sheet = workbook_sheet_by_index (book, i);
			sheet_mark_dirty (sheet);
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
	CmdPrintSetup *me;

	me = g_object_new (CMD_PRINT_SETUP_TYPE, NULL);

	me->cmd.sheet = sheet;
	me->cmd.size = 10;
	if (sheet)
		me->cmd.cmd_descriptor =
			g_strdup_printf (_("Page Setup For %s"), sheet->name_unquoted);
	else
		me->cmd.cmd_descriptor = g_strdup (_("Page Setup For All Sheets"));
	me->old_pi = NULL;
	me->new_pi = print_info_dup ((PrintInformation *) pi);

	return command_push_undo (wbc, G_OBJECT (me));
}

/******************************************************************/

#define CMD_DEFINE_NAME_TYPE        (cmd_define_name_get_type ())
#define CMD_DEFINE_NAME(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_DEFINE_NAME_TYPE, CmdDefineName))

typedef struct {
	GnmCommand cmd;

	GnmParsePos	 pp;
	char		*name;
	GnmExprTop const *texpr;
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
	GnmExprTop const *texpr = nexpr->texpr;

	gnm_expr_top_ref (texpr);
	if (me->new_name)
		expr_name_remove (nexpr);
	else if (me->placeholder)
		expr_name_downgrade_to_placeholder (nexpr);
	else
		expr_name_set_expr (nexpr, me->texpr); /* restore old def */

	me->texpr = texpr;
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
		nexpr = expr_name_add (&me->pp, me->name, me->texpr, &err, TRUE, NULL);
		if (nexpr == NULL) {
			go_cmd_context_error_invalid (GO_CMD_CONTEXT (wbc), _("Name"), err);
			g_free (err);
			return TRUE;
		}
		me->texpr = NULL;
	} else {	/* changing the definition */
		GnmExprTop const *tmp = nexpr->texpr;
		gnm_expr_top_ref (tmp);
		expr_name_set_expr (nexpr, me->texpr);
		me->texpr = tmp; /* store the old definition */
	}

	return FALSE;
}

static void
cmd_define_name_finalize (GObject *cmd)
{
	CmdDefineName *me = CMD_DEFINE_NAME (cmd);

	g_free (me->name); me->name = NULL;

	if (me->texpr) {
		gnm_expr_top_unref (me->texpr);
		me->texpr = NULL;
	}

	gnm_command_finalize (cmd);
}

/**
 * cmd_define_name :
 * @wbc :
 * @name :
 * @pp   :
 * @texpr : absorbs a ref to the texpr.
 * @descriptor : optional descriptor.
 *
 * If the @name has never been defined in context @pp create a new name
 * If its a placeholder assign @texpr to it and make it real
 * If it already exists as a real name just assign @expr.
 *
 * Returns TRUE on error
 **/
gboolean
cmd_define_name (WorkbookControl *wbc, char const *name,
		 GnmParsePos const *pp, GnmExprTop const *texpr,
		 char const *descriptor)
{
	CmdDefineName	*me;
	GnmNamedExpr    *nexpr;

	g_return_val_if_fail (name != NULL, TRUE);
	g_return_val_if_fail (pp != NULL, TRUE);
	g_return_val_if_fail (texpr != NULL, TRUE);

	if (!expr_name_validate (name)) {
		go_cmd_context_error_invalid (GO_CMD_CONTEXT (wbc), name,
					_("is not allowed as defined name"));
		gnm_expr_top_unref (texpr);
		return TRUE;
	}

	if (expr_name_check_for_loop (name, texpr)) {
		go_cmd_context_error_invalid (GO_CMD_CONTEXT (wbc), name,
					_("has a circular reference"));
		gnm_expr_top_unref (texpr);
		return TRUE;
	}
	nexpr = expr_name_lookup (pp, name);
	if (nexpr != NULL && !expr_name_is_placeholder (nexpr) &&
	    gnm_expr_top_equal (texpr, nexpr->texpr)) {
		gnm_expr_top_unref (texpr);
		return FALSE; /* expr is not changing, do nothing */
	}

	me = g_object_new (CMD_DEFINE_NAME_TYPE, NULL);
	me->name = g_strdup (name);
	me->pp = *pp;
	me->texpr = texpr;

	me->cmd.sheet = wb_control_cur_sheet (wbc);
	me->cmd.size = 1;

	if (descriptor == NULL) {
		char const *tmp;
		GString *res;

	/* Underscores need to be doubled.  */
		res = g_string_new (NULL);
		for (tmp = name; *tmp; tmp++) {
			if (*tmp == '_')
				g_string_append_c (res, '_');
			g_string_append_c (res, *tmp);
		}

		nexpr = expr_name_lookup (pp, name);
		if (nexpr == NULL || expr_name_is_placeholder (nexpr))
			me->cmd.cmd_descriptor =
				g_strdup_printf (_("Define Name %s"), res->str);
		else
			me->cmd.cmd_descriptor =
				g_strdup_printf (_("Update Name %s"), res->str);
		g_string_free (res, TRUE);
	} else
		me->cmd.cmd_descriptor = g_strdup (descriptor);

	return command_push_undo (wbc, G_OBJECT (me));
}

/******************************************************************/

#define CMD_REMOVE_NAME_TYPE        (cmd_remove_name_get_type ())
#define CMD_REMOVE_NAME(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_REMOVE_NAME_TYPE, CmdRemoveName))

typedef struct {
	GnmCommand cmd;

	GnmParsePos pp;
	GnmNamedExpr *nexpr;
	const GnmExprTop *texpr;
} CmdRemoveName;

MAKE_GNM_COMMAND (CmdRemoveName, cmd_remove_name, NULL);

static gboolean
cmd_remove_name_undo (GnmCommand *cmd,
		      G_GNUC_UNUSED WorkbookControl *wbc)
{
	CmdRemoveName *me = CMD_REMOVE_NAME (cmd);
	GnmNamedExpr *nexpr =
		expr_name_add (&me->nexpr->pos, me->nexpr->name->str,
			       me->texpr, NULL, TRUE, NULL);
	if (nexpr) {
		me->texpr = NULL;
		expr_name_ref (nexpr);
		expr_name_unref (me->nexpr);
		me->nexpr = nexpr;
		return FALSE;
	} else {
		g_warning ("Redefining name failed.");
		return TRUE;
	}
}

static gboolean
cmd_remove_name_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdRemoveName *me = CMD_REMOVE_NAME (cmd);

	me->texpr = me->nexpr->texpr;
	gnm_expr_top_ref (me->texpr);
	expr_name_downgrade_to_placeholder (me->nexpr);

	return FALSE;
}

static void
cmd_remove_name_finalize (GObject *cmd)
{
	CmdRemoveName *me = CMD_REMOVE_NAME (cmd);

	expr_name_unref (me->nexpr);

	if (me->texpr) {
		gnm_expr_top_unref (me->texpr);
		me->texpr = NULL;
	}

	gnm_command_finalize (cmd);
}

/**
 * cmd_remove_name :
 * @wbc :
 * @nexpr : name to remove.
 *
 * Returns TRUE on error
 **/
gboolean
cmd_remove_name (WorkbookControl *wbc, GnmNamedExpr *nexpr)
{
	CmdRemoveName *me;

	g_return_val_if_fail (wbc != NULL, TRUE);
	g_return_val_if_fail (nexpr != NULL, TRUE);
	g_return_val_if_fail (!expr_name_is_placeholder (nexpr), TRUE);

	expr_name_ref (nexpr);

	me = g_object_new (CMD_REMOVE_NAME_TYPE, NULL);
	me->nexpr = nexpr;
	me->texpr = NULL;
	me->cmd.sheet = wb_control_cur_sheet (wbc);
	me->cmd.size = 1;
	me->cmd.cmd_descriptor = g_strdup_printf (_("Remove Name %s"), nexpr->name->str);

	return command_push_undo (wbc, G_OBJECT (me));
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

	g_return_val_if_fail (IS_WORKBOOK_CONTROL (wbc), TRUE);
	g_return_val_if_fail (IS_SHEET (sheet), TRUE);

	me = g_object_new (CMD_SCENARIO_ADD_TYPE, NULL);

	me->scenario  = s;
	me->cmd.sheet = sheet;
	me->cmd.size  = 1;
	me->cmd.cmd_descriptor = g_strdup (_("Add scenario"));

	return command_push_undo (wbc, G_OBJECT (me));
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
	data_analysis_output_t dao;

	g_return_val_if_fail (IS_WORKBOOK_CONTROL (wbc), TRUE);
	g_return_val_if_fail (IS_SHEET (sheet), TRUE);

	me = g_object_new (CMD_SCENARIO_MNGR_TYPE, NULL);

	me->sc = sc;
	me->cmd.sheet = sheet;
	me->cmd.size  = 1;
	me->cmd.cmd_descriptor = g_strdup (_("Scenario Show"));

	dao_init (&dao, NewSheetOutput);
	dao.sheet = me->cmd.sheet;
	me->sc->redo = scenario_show (wbc, me->sc->undo, NULL, &dao);

	return command_push_undo (wbc, G_OBJECT (me));
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

	g_return_val_if_fail (IS_WORKBOOK_CONTROL (wbc), TRUE);
	g_return_val_if_fail (IS_SHEET (sheet), TRUE);

	me = g_object_new (CMD_DATA_SHUFFLE_TYPE, NULL);

	me->ds        = sc;
	me->cmd.sheet = sheet;
	me->cmd.size  = 1;
	me->cmd.cmd_descriptor = g_strdup (_("Shuffle Data"));

	return command_push_undo (wbc, G_OBJECT (me));
}

/******************************************************************/

#define CMD_TEXT_TO_COLUMNS_TYPE        (cmd_text_to_columns_get_type ())
#define CMD_TEXT_TO_COLUMNS(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_TEXT_TO_COLUMNS_TYPE, CmdTextToColumns))

typedef struct {
	GnmCommand cmd;

	GnmCellRegion      *contents;
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
	GnmCellRegion *contents;

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->contents != NULL, TRUE);

	contents = clipboard_copy_range (me->dst.sheet, &me->dst.range);
	if (clipboard_paste_region (me->contents, &me->dst, GO_CMD_CONTEXT (wbc))) {
		/* There was a problem, avoid leaking */
		cellregion_unref (contents);
		return TRUE;
	}

	cellregion_unref (me->contents);

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

	me->contents = contents;

	/* Select the newly pasted contents  (this queues a redraw) */
	select_range (me->dst.sheet, &me->dst.range, wbc);

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
	if (me->contents) {
		cellregion_unref (me->contents);
		me->contents = NULL;
	}
	gnm_command_finalize (cmd);
}

gboolean
cmd_text_to_columns (WorkbookControl *wbc,
		     GnmRange const *src, Sheet *src_sheet,
		     GnmRange const *target, Sheet *target_sheet,
		     GnmCellRegion *contents)
{
	CmdTextToColumns *me;
	char *src_range_name, *target_range_name;

	g_return_val_if_fail (contents != NULL, TRUE);

	src_range_name = undo_range_name (src_sheet, src);
	target_range_name = undo_range_name (target_sheet, target);

	me = g_object_new (CMD_TEXT_TO_COLUMNS_TYPE, NULL);

	me->cmd.sheet = (src_sheet == target_sheet ? src_sheet : NULL);
	me->cmd.size = 1;  /* FIXME?  */
	me->cmd.cmd_descriptor = g_strdup_printf (_("Text (%s) to Columns (%s)"),
						  src_range_name,
						  target_range_name);
	me->dst.range = *target;
	me->dst.sheet = target_sheet;
	me->dst.paste_flags = PASTE_CONTENTS | PASTE_FORMATS;
	me->src = *src;
	me->src_sheet = src_sheet;
	me->contents = contents;
	me->saved_sizes = NULL;

	g_free (src_range_name);
	g_free (target_range_name);

	/* Check array subdivision & merged regions */
	if (sheet_range_splits_region (target_sheet, &me->dst.range,
				       NULL, GO_CMD_CONTEXT (wbc), me->cmd.cmd_descriptor)) {
		g_object_unref (me);
		return TRUE;
	}

	return command_push_undo (wbc, G_OBJECT (me));
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
	go_slist_free_custom (v, g_free);
}

static void
cmd_solver_finalize (GObject *cmd)
{
	CmdSolver *me = CMD_SOLVER (cmd);

	g_slist_free (me->cells);
	me->cells = NULL;
	go_slist_free_custom (me->ov, (GFreeFunc)cmd_solver_free_values);
	me->ov = NULL;
	go_slist_free_custom (me->nv, (GFreeFunc)cmd_solver_free_values);
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
	CmdSolver *me;

	g_return_val_if_fail (cells != NULL, TRUE);
	g_return_val_if_fail (ov != NULL || nv != NULL, TRUE);

	me = g_object_new (CMD_SOLVER_TYPE, NULL);

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

	return command_push_undo (wbc, G_OBJECT (me));
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
	CmdGoalSeek *me;
	GnmRange range;

	g_return_val_if_fail (cell != NULL, TRUE);
	g_return_val_if_fail (ov != NULL || nv != NULL, TRUE);

	me = g_object_new (CMD_GOAL_SEEK_TYPE, NULL);

	me->cmd.sheet = cell->base.sheet;
	me->cmd.size = 1;
	range_init_cellpos (&range, &cell->pos);
	me->cmd.cmd_descriptor = g_strdup_printf
		(_("Goal Seek (%s)"), undo_range_name (cell->base.sheet, &range));

	me->cell = cell;
	me->ov = ov;
	me->nv = nv;

	if (me->ov == NULL)
		me->ov = value_dup (cell->value);
	if (me->nv == NULL)
		me->nv = value_dup (cell->value);

	return command_push_undo (wbc, G_OBJECT (me));
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
	CmdFreezePanes	*me;

	g_return_val_if_fail (name != NULL, TRUE);
	g_return_val_if_fail (pp != NULL, TRUE);
	g_return_val_if_fail (expr != NULL, TRUE);

	me = g_object_new (CMD_FREEZE_PANES_TYPE, NULL);
	me->sv = sv;
	me->frozen   = f;
	me->unfrozen = expr;
	return command_push_undo (wbc, G_OBJECT (me));
}

#endif


/******************************************************************/


#define CMD_TABULATE_TYPE        (cmd_tabulate_get_type ())
#define CMD_TABULATE(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_TABULATE_TYPE, CmdTabulate))

typedef struct {
	GnmCommand cmd;
	GSList *sheet_idx;
	GnmTabulateInfo *data;
} CmdTabulate;

MAKE_GNM_COMMAND (CmdTabulate, cmd_tabulate, NULL);

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

static gboolean
cmd_tabulate_undo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdTabulate *me = CMD_TABULATE (cmd);
	GSList *l;
	gboolean res = TRUE;

	me->sheet_idx  = g_slist_sort (me->sheet_idx,
				       cmd_reorganize_sheets_delete_cmp_f);

	for (l = me->sheet_idx; l != NULL; l = l->next) {
		int i = GPOINTER_TO_INT (l->data);
		Sheet *new_sheet =
			workbook_sheet_by_index (wb_control_get_workbook (wbc),
						 i);
		res = res && command_undo_sheet_delete (new_sheet);
	}
	return !res;
}

static gboolean
cmd_tabulate_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdTabulate *me = CMD_TABULATE (cmd);

	g_slist_free (me->sheet_idx);
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
	CmdTabulate *me;

	g_return_val_if_fail (data != NULL, TRUE);

	me = g_object_new (CMD_TABULATE_TYPE, NULL);

	me->cmd.sheet = NULL;
	me->cmd.size = 1;
	me->cmd.cmd_descriptor =
		g_strdup_printf (_("Tabulating Dependencies"));
	me->data = data;
	me->sheet_idx = NULL;

	return command_push_undo (wbc, G_OBJECT (me));
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

	g_object_unref (me->so);
	g_object_unref (me->new_graph);
	g_object_unref (me->old_graph);

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

#define CMD_TOGGLE_RTL_TYPE (cmd_toggle_rtl_get_type ())
#define CMD_TOGGLE_RTL(o)   (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_TOGGLE_RTL_TYPE, CmdToggleRTL))

typedef GnmCommand CmdToggleRTL;

MAKE_GNM_COMMAND (CmdToggleRTL, cmd_toggle_rtl, NULL);

static gboolean
cmd_toggle_rtl_redo (GnmCommand *cmd, G_GNUC_UNUSED WorkbookControl *wbc)
{
	go_object_toggle (cmd->sheet, "text-is-rtl");
	return FALSE;
}

static gboolean
cmd_toggle_rtl_undo (GnmCommand *cmd, G_GNUC_UNUSED WorkbookControl *wbc)
{
	return cmd_toggle_rtl_redo (cmd, wbc);
}

static void
cmd_toggle_rtl_finalize (GObject *cmd)
{
	gnm_command_finalize (cmd);
}

gboolean
cmd_toggle_rtl (WorkbookControl *wbc, Sheet *sheet)
{
	CmdToggleRTL *me;

	g_return_val_if_fail (IS_WORKBOOK_CONTROL (wbc), TRUE);
	g_return_val_if_fail (IS_SHEET (sheet), TRUE);

	me = g_object_new (CMD_TOGGLE_RTL_TYPE, NULL);
	me->sheet = sheet;
	me->size = 1;
	me->cmd_descriptor = g_strdup (sheet->text_is_rtl ? _("Left to Right") : _("Right to Left"));

	return command_push_undo (wbc, G_OBJECT (me));
}

/******************************************************************/

#define CMD_SO_SET_VALUE_TYPE (cmd_so_set_value_get_type ())
#define CMD_SO_SET_VALUE(o)   (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_SO_SET_VALUE_TYPE, CmdSOSetValue))

typedef struct {
	GnmCommand cmd;
	GnmCellRef ref;
	GnmValue *val;
	GOUndo *undo;
} CmdSOSetValue;

MAKE_GNM_COMMAND (CmdSOSetValue, cmd_so_set_value, NULL);

static gboolean
cmd_so_set_value_redo (GnmCommand *cmd, G_GNUC_UNUSED WorkbookControl *wbc)
{
	CmdSOSetValue *me = CMD_SO_SET_VALUE (cmd);
	Sheet *sheet = me->ref.sheet;
	GnmCell *cell = sheet_cell_fetch (sheet, me->ref.col, me->ref.row);

	sheet_cell_set_value (cell, value_dup (me->val));
	workbook_recalc (sheet->workbook);
	sheet_update (sheet);

	return FALSE;
}

static gboolean
cmd_so_set_value_undo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdSOSetValue *me = CMD_SO_SET_VALUE (cmd);

	go_undo_undo_with_data (me->undo, GO_CMD_CONTEXT (wbc));

	return FALSE;
}

static void
cmd_so_set_value_finalize (GObject *cmd)
{
	CmdSOSetValue *me = CMD_SO_SET_VALUE (cmd);

	value_release (me->val);
	g_object_unref (me->undo);

	gnm_command_finalize (cmd);
}

gboolean
cmd_so_set_value (WorkbookControl *wbc,
		  const char *text,
		  const GnmCellRef *pref,
		  GnmValue *new_val)
{
	CmdSOSetValue *me;
	GnmRange r;

	g_return_val_if_fail (IS_WORKBOOK_CONTROL (wbc), TRUE);

	r.start.col = r.end.col = pref->col;
	r.start.row = r.end.row = pref->row;

	me = g_object_new (CMD_SO_SET_VALUE_TYPE, NULL);
	me->cmd.sheet = pref->sheet;
	me->cmd.size = 1;
	me->cmd.cmd_descriptor = g_strdup (text);
	me->ref = *pref;
	me->val = new_val;
	me->undo = clipboard_copy_range_undo (pref->sheet, &r);

	return command_push_undo (wbc, G_OBJECT (me));
}

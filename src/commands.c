/*
 * command.c : Handlers to undo & redo commands
 *
 * Author:
 * 	Jody Goldberg <jgoldberg@home.com>
 *
 * (C) 1999, 2000 Jody Goldberg
 */
#include <config.h>
#include "gnumeric-type-util.h"
#include "commands.h"
#include "sheet.h"
#include "workbook-view.h"
#include "utils.h"

/*
 * NOTE : This is a work in progress
 * Only the SetText command is complete and working
 * insert/delete row/col is connected but not implemented.
 * the rest need to be filled in.
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
 * I would like some style utilies.
 * For insert/delete & clear I'd like get a range clipped list of
 * copies of all the styles that apply to a given area.  The application
 * regions for the copies should also be clipped.
 *
 * For formatting.  I'd like a range clipped list of a subset of style
 * elements corresponding to those being overridden.
 *
 * That way undo redo just become applications of the old or the new styles.
 *
 * FIXME : fine tune the use of these commands so that they actually
 *         execute the operation.  Redo must be capable of applying the
 *         command, so lets avoid duplicating logic.  SetText is a good
 *         example of the WRONG way to do it.  I use that as an after thought
 *         and hence treat formating differenting.
 *
 * TODO : Add user preference for undo buffer size limit (# of commands ?)
 * TODO : Possibly clear lists on save.
 *
 * TODO : Reqs for selective undo
 * TODO : Add Repeat last command
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
	GnumericCommand *cmd = GNUMERIC_COMMAND(obj);

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

	g_return_if_fail (wb != NULL);
	g_return_if_fail (wb->undo_commands != NULL);

	cmd = GNUMERIC_COMMAND(wb->undo_commands->data);
	g_return_if_fail (cmd != NULL);

	wb->undo_commands = g_slist_remove (wb->undo_commands,
					    wb->undo_commands->data);
	GNUMERIC_COMMAND_CLASS(cmd->parent.klass)->undo_cmd (cmd, context);
	wb->redo_commands = g_slist_prepend (wb->redo_commands, cmd);
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

	g_return_if_fail (wb);
	g_return_if_fail (wb->redo_commands);

	cmd = GNUMERIC_COMMAND(wb->redo_commands->data);
	g_return_if_fail (cmd != NULL);

	/* Remove the command from the undo list */
	wb->redo_commands = g_slist_remove (wb->redo_commands,
					    wb->redo_commands->data);
	GNUMERIC_COMMAND_CLASS(cmd->parent.klass)->redo_cmd (cmd, context);
	wb->undo_commands = g_slist_prepend (wb->undo_commands, cmd);
	undo_redo_menu_labels (wb);
}

/*
 * command_list_pop_top : utility routine to free the top command on
 *        the undo list, and to regenerate the menus if needed.
 *
 * @cmd_list : The set of commands to free from.
 */
void
command_list_pop_top_undo (Workbook *wb)
{
	GtkObject *cmd;
	
	g_return_if_fail (wb->undo_commands != NULL);

	cmd = GTK_OBJECT (wb->undo_commands->data);
	g_return_if_fail (cmd != NULL);

	gtk_object_unref (cmd);
	wb->undo_commands = g_slist_remove (wb->undo_commands,
					    wb->undo_commands->data);
	undo_redo_menu_labels (wb);
}

/*
 * command_list_release : utility routine to free the resources associated
 *    with a list of commands.
 *
 * @cmd_list : The set of commands to free.
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
 * @wb : The workbook the command operated on.
 * @cmd : The new command to add.
 * @trouble : A flag indicating whether there was a problem with the
 *            command.
 *
 * returns : @trouble.
 */
static gboolean
command_push_undo (Workbook *wb, GtkObject *cmd, gboolean const trouble)
{
	/* TODO : trouble should be a variable not an argument.
	 * We should call redo on the command object and use
	 * the result as the value for trouble.
	 */
	if  (!trouble) {
		g_return_val_if_fail (wb != NULL, TRUE);
		g_return_val_if_fail (cmd != NULL, TRUE);

		command_list_release (wb->redo_commands);
		wb->redo_commands = NULL;

		wb->undo_commands = g_slist_prepend (wb->undo_commands, cmd);

		undo_redo_menu_labels (wb);
	} else
		gtk_object_unref (cmd);

	return trouble;
}

/******************************************************************/

#define CMD_SET_TEXT_TYPE        (cmd_set_text_get_type ())
#define CMD_SET_TEXT(o)          (GTK_CHECK_CAST ((o), CMD_SET_TEXT_TYPE, CmdSetText))

typedef struct
{
	GnumericCommand parent;

	EvalPosition	 pos;
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

	/* Move back to the cell that was edited */
	sheet_cursor_move (me->pos.sheet,
			   me->pos.eval.col,
			   me->pos.eval.row,
			   TRUE, TRUE);

	/* Save the new value so we can redo */
	cell = sheet_cell_get (me->pos.sheet,
			       me->pos.eval.col,
			       me->pos.eval.row);

	g_return_val_if_fail (cell != NULL, TRUE);

	new_text = cell_get_text (cell);

	/* Restore the old value (possibly empty) */
	if (me->text != NULL) {
		cell_set_text (cell, me->text);
		g_free (me->text);
	} else
		cell_set_value (cell, value_new_empty ());

	me->text = new_text;
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
	if (me->text != NULL)
		g_free (me->text);
	gnumeric_command_destroy (cmd);
}

gboolean
cmd_set_text (CommandContext *context,
	      Sheet *sheet, CellPos const * const pos,
	      char const * const new_text,
	      String const * const old_text)
{
	static int const max_descriptor_width = 15;

	GtkObject *obj;
	CmdSetText *me;
	gchar *pad = "";
	gchar *text; 

	g_return_val_if_fail (sheet != NULL, TRUE);
	g_return_val_if_fail (new_text != NULL, TRUE);

	obj = gtk_type_new (CMD_SET_TEXT_TYPE);
	me = CMD_SET_TEXT (obj);

	/* Store the specs for the object */
	me->pos.sheet = sheet;
	me->pos.eval = *pos;
	if (old_text != NULL)
		me->text = g_strdup (old_text->str);
	else
		me->text = NULL;

	/* Limit the size of the descriptor to something reasonable */
	if (strlen(new_text) > max_descriptor_width) {
		pad = "..."; /* length of 3 */
		text = g_strndup (new_text, max_descriptor_width - 3);
	} else
		text = (gchar *)new_text;

	me->parent.cmd_descriptor =
	    g_strdup_printf (_("Typing \"%s%s\" in %s"), text, pad,
			     cell_name(pos->col, pos->row));

	if (*pad) g_free (text);

	/* Register the command object */
	return command_push_undo (sheet->workbook, obj, FALSE);
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
} CmdInsDelRowCol;

GNUMERIC_MAKE_COMMAND (CmdInsDelRowCol, cmd_ins_del_row_col);

static gboolean
cmd_ins_del_row_col_undo (GnumericCommand *cmd, CommandContext *context)
{
	CmdInsDelRowCol *me = CMD_INS_DEL_ROW_COL(cmd);
	int index;

	g_return_val_if_fail (me != NULL, TRUE);

	/* TODO : 1) Restore the values of the deleted cells */
	/* TODO : 2) Restore the styles in the cleared range */
	if (!me->is_insert) {
		index = me->index;
		if (me->is_cols)
			sheet_insert_cols (context, me->sheet, me->index, me->count);
		else
			sheet_insert_rows (context, me->sheet, me->index, me->count);
	} else {
		index = ((me->is_cols) ? SHEET_MAX_COLS : SHEET_MAX_ROWS) - me->count;
		if (me->is_cols)
			sheet_delete_cols (context, me->sheet, me->index, me->count);
		else
			sheet_delete_rows (context, me->sheet, me->index, me->count);
	}
	sheet_restore_row_col_sizes (me->sheet, me->is_cols, index, me->count,
				     me->sizes);
	me->sizes = NULL;

	return FALSE;
}

static gboolean
cmd_ins_del_row_col_redo (GnumericCommand *cmd, CommandContext *context)
{
	CmdInsDelRowCol *me = CMD_INS_DEL_ROW_COL(cmd);

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->sizes == NULL, TRUE);

	/* TODO : 1) Save the values of the deleted cells */
	/* TODO : 2) Save the styles in the cleared range */
	if (me->is_insert) {
		int const index = ((me->is_cols) ? SHEET_MAX_COLS : SHEET_MAX_ROWS) -
			me->count;
		me->sizes = sheet_save_row_col_sizes (me->sheet, me->is_cols,
						      index, me->count);
		if (me->is_cols)
			sheet_insert_cols (context, me->sheet, me->index, me->count);
		else
			sheet_insert_rows (context, me->sheet, me->index, me->count);
	} else {
		me->sizes = sheet_save_row_col_sizes (me->sheet, me->is_cols,
						      me->index, me->count);
		if (me->is_cols)
			sheet_delete_cols (context, me->sheet, me->index, me->count);
		else
			sheet_delete_rows (context, me->sheet, me->index, me->count);
	}

	return FALSE;
}
static void
cmd_ins_del_row_col_destroy (GtkObject *cmd)
{
	CmdInsDelRowCol *me = CMD_INS_DEL_ROW_COL(cmd);

	if (me->sizes)
		g_free (me->sizes);
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
	gboolean trouble;

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

	me->parent.cmd_descriptor = descriptor;

	trouble = cmd_ins_del_row_col_redo (GNUMERIC_COMMAND(me), context);

	/* Register the command object */
	return command_push_undo (sheet->workbook, obj, trouble);
}

gboolean
cmd_insert_cols (CommandContext *context,
		 Sheet *sheet, int start_col, int count)
{
	char *mesg = g_strdup_printf (_("Inserting %d column%s at %s"), count,
				      (count > 1) ? "s" : "", col_name(start_col));
	return cmd_ins_del_row_col (context, sheet, TRUE, TRUE, mesg,
				    start_col, count);
}

gboolean
cmd_insert_rows (CommandContext *context,
		 Sheet *sheet, int start_row, int count)
{
	char *mesg = g_strdup_printf (_("Inserting %d row%s at %d"), count,
				      (count > 1) ? "s" : "", start_row+1);
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
		char *temp = g_strdup_printf (_("Deleting %d columns (%s:"),
					      count, col_name(start_col));
		mesg = g_strconcat (temp, col_name(start_col+count-1), ")", NULL);
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
	    ? g_strdup_printf (_("Deleting %d rows (%d:%d"), count, start_row,
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
} CmdClear;

GNUMERIC_MAKE_COMMAND (CmdClear, cmd_clear);

static gboolean
cmd_clear_undo (GnumericCommand *cmd, CommandContext *context)
{
	CmdClear *me = CMD_CLEAR(cmd);

	g_return_val_if_fail (me != NULL, TRUE);

	/* FIXME : Fill in */
	return FALSE;
}

static gboolean
cmd_clear_redo (GnumericCommand *cmd, CommandContext *context)
{
	CmdClear *me = CMD_CLEAR(cmd);

	g_return_val_if_fail (me != NULL, TRUE);

	/* FIXME : Fill in */
	return FALSE;
}
static void
cmd_clear_destroy (GtkObject *cmd)
{
#if 0
	CmdClear *me = CMD_CLEAR(cmd);
#endif
	/* FIXME : Fill in */
	gnumeric_command_destroy (cmd);
}

#if 0
gboolean
cmd_clear (CommandContext *context,
{
	return FALSE;
}
#endif

/******************************************************************/

#define CMD_PASTE_COPY_TYPE        (cmd_paste_copy_get_type ())
#define CMD_PASTE_COPY(o)          (GTK_CHECK_CAST ((o), CMD_PASTE_COPY_TYPE, CmdPasteCopy))

typedef struct
{
	GnumericCommand parent;
} CmdPasteCopy;

GNUMERIC_MAKE_COMMAND (CmdPasteCopy, cmd_paste_copy);

static gboolean
cmd_paste_copy_undo (GnumericCommand *cmd, CommandContext *context)
{
	CmdPasteCopy *me = CMD_PASTE_COPY(cmd);

	g_return_val_if_fail (me != NULL, TRUE);

	/* FIXME : Fill in */
	return FALSE;
}

static gboolean
cmd_paste_copy_redo (GnumericCommand *cmd, CommandContext *context)
{
	CmdPasteCopy *me = CMD_PASTE_COPY(cmd);

	g_return_val_if_fail (me != NULL, TRUE);

	/* FIXME : Fill in */
	return FALSE;
}
static void
cmd_paste_copy_destroy (GtkObject *cmd)
{
#if 0
	CmdPasteCopy *me = CMD_PASTE_COPY(cmd);
#endif
	/* FIXME : Fill in */
	gnumeric_command_destroy (cmd);
}

#if 0
gboolean
cmd_paste_copy (CommandContext *context,
{
	return FALSE;
}
#endif

/******************************************************************/

#define CMD_PASTE_CUT_TYPE        (cmd_paste_cut_get_type ())
#define CMD_PASTE_CUT(o)          (GTK_CHECK_CAST ((o), CMD_PASTE_CUT_TYPE, CmdPasteCut))

typedef struct
{
	GnumericCommand parent;
} CmdPasteCut;

GNUMERIC_MAKE_COMMAND (CmdPasteCut, cmd_paste_cut);

static gboolean
cmd_paste_cut_undo (GnumericCommand *cmd, CommandContext *context)
{
	CmdPasteCut *me = CMD_PASTE_CUT(cmd);

	g_return_val_if_fail (me != NULL, TRUE);

	/* FIXME : Fill in */
	return FALSE;
}

static gboolean
cmd_paste_cut_redo (GnumericCommand *cmd, CommandContext *context)
{
	CmdPasteCut *me = CMD_PASTE_CUT(cmd);

	g_return_val_if_fail (me != NULL, TRUE);

	/* FIXME : Fill in */
	return FALSE;
}
static void
cmd_paste_cut_destroy (GtkObject *cmd)
{
#if 0
	CmdPasteCut *me = CMD_PASTE_CUT(cmd);
#endif
	/* FIXME : Fill in */
	gnumeric_command_destroy (cmd);
}

gboolean
cmd_paste_cut (CommandContext *context, ExprRelocateInfo const * const info)
{
	GtkObject *obj;
	CmdPasteCut *me;
	gboolean trouble;

	/* FIXME : improve on this */
	char *descriptor = g_strdup_printf (_("Moving cells") );

	g_return_val_if_fail (info != NULL, TRUE);

	obj = gtk_type_new (CMD_PASTE_CUT_TYPE);
	me = CMD_PASTE_CUT (obj);

	/* Store the specs for the object */

	me->parent.cmd_descriptor = descriptor;

	trouble = cmd_paste_cut_redo (GNUMERIC_COMMAND(me), context);

	/* Register the command object */
	/* NOTE : if the destination workbook is different from the source workbook
	 * should we have undo elements in both menus ??  It seems poor form to
	 * hit undo in 1 window and effect another ...
	 */
	return command_push_undo (info->target_sheet->workbook, obj, trouble);
}

/******************************************************************/

#define CMD_FORMAT_TYPE        (cmd_format_get_type ())
#define CMD_FORMAT(o)          (GTK_CHECK_CAST ((o), CMD_FORMAT_TYPE, CmdFormat))

typedef struct
{
	GnumericCommand parent;
} CmdFormat;

GNUMERIC_MAKE_COMMAND (CmdFormat, cmd_format);

static gboolean
cmd_format_undo (GnumericCommand *cmd, CommandContext *context)
{
	CmdFormat *me = CMD_FORMAT(cmd);

	g_return_val_if_fail (me != NULL, TRUE);

	/* FIXME : Fill in */
	return FALSE;
}

static gboolean
cmd_format_redo (GnumericCommand *cmd, CommandContext *context)
{
	CmdFormat *me = CMD_FORMAT(cmd);

	g_return_val_if_fail (me != NULL, TRUE);

	/* FIXME : Fill in */
	return FALSE;
}
static void
cmd_format_destroy (GtkObject *cmd)
{
#if 0
	CmdFormat *me = CMD_FORMAT(cmd);
#endif

	/* FIXME : Fill in */
	gnumeric_command_destroy (cmd);
}

#if 0
gboolean
cmd_format (CommandContext *context,
{
	return FALSE;
}
#endif

/* TODO : Make a list of commands that should have undo support that dont
 *        even have stubs
 * - Rename sheet
 * - Autofill
 * - Array formula creation.
 */

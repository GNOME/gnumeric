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
#include "clipboard.h"
#include "selection.h"
#include "datetime.h"

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
 * returns : TRUE if there was an error.
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
	if (me->text != NULL) {
		g_free (me->text);
		me->text = NULL;
	}
	gnumeric_command_destroy (cmd);
}

gboolean
cmd_set_text (CommandContext *context,
	      Sheet *sheet, CellPos const * const pos,
	      char * new_text,
	      String const * const old_text)
{
	static int const max_descriptor_width = 15;

	GtkObject *obj;
	CmdSetText *me;
	gchar *pad = "";
	gchar *text;

	g_return_val_if_fail (sheet != NULL, TRUE);
	g_return_val_if_fail (new_text != NULL, TRUE);

	/* From src/dialogs/dialog-autocorrect.c */
	autocorrect_tool (new_text);
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
		text = g_strndup (new_text,
				  max_descriptor_width - 3);
	} else
		text = (gchar *) new_text;

	me->parent.cmd_descriptor =
	    g_strdup_printf (_("Typing \"%s%s\" in %s"), text, pad,
			     cell_name(pos->col, pos->row));

	if (*pad)
		g_free (text);

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
		clipboard_paste_region (context, me->contents, me->sheet,
					index, 0, PASTE_ALL_TYPES,
					GDK_CURRENT_TIME);
	else
		clipboard_paste_region (context, me->contents, me->sheet,
					0, index, PASTE_ALL_TYPES,
					GDK_CURRENT_TIME);
	clipboard_release (me->contents);
	me->contents = NULL;

	/* Throw away the undo info for the expressions after the action*/
	workbook_expr_unrelocate_free (tmp);

	/* Restore the changed expressions before the action */
	workbook_expr_unrelocate (me->sheet->workbook, me->reloc_storage);
	me->reloc_storage = NULL;

	workbook_recalc (me->sheet->workbook);
	sheet_redraw_all (me->sheet);
	sheet_load_cell_val (me->sheet);

	return trouble;
}

static gboolean
cmd_ins_del_row_col_redo (GnumericCommand *cmd, CommandContext *context)
{
	CmdInsDelRowCol *me = CMD_INS_DEL_ROW_COL(cmd);
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
	me->contents = (me->is_cols)
	    ? clipboard_copy_cell_range (me->sheet,
					 index,			0,
					 index + me->count - 1,	SHEET_MAX_ROWS-1)
	    : clipboard_copy_cell_range  (me->sheet,
					  0,			index,
					  SHEET_MAX_COLS-1,	index + me->count - 1);

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

	workbook_recalc (me->sheet->workbook);
	sheet_redraw_all (me->sheet);
	sheet_load_cell_val (me->sheet);

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
	me->contents = NULL;

	me->parent.cmd_descriptor = descriptor;

	trouble = cmd_ins_del_row_col_redo (GNUMERIC_COMMAND(me), context);

	/* Register the command object */
	return command_push_undo (sheet->workbook, obj, trouble);
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
		CellRegion * c;

		g_return_val_if_fail (me->old_content != NULL, TRUE);

		c = me->old_content->data;
		clipboard_paste_region (context, c, me->sheet,
					r->start.col, r->start.row,
					me->paste_flags,
					GDK_CURRENT_TIME);
		clipboard_release (c);
		me->old_content = g_slist_remove (me->old_content, c);
	}
	g_return_val_if_fail (me->old_content == NULL, TRUE);

	workbook_recalc (me->sheet->workbook);
	sheet_load_cell_val (me->sheet);

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

	for (l = me->selection ; l != NULL ; l = l->next) {
		Range const * const r = l->data;
		me->old_content =
			g_slist_prepend (me->old_content,
				clipboard_copy_cell_range (me->sheet,
							   r->start.col, r->start.row,
							   r->end.col, r->end.row));

		sheet_clear_region (context, me->sheet,
				    r->start.col, r->start.row,
				    r->end.col, r->end.row,
				    me->clear_flags);
	}

	workbook_recalc (me->sheet->workbook);
	sheet_load_cell_val (me->sheet);

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
cmd_clear_selection (CommandContext *context, Sheet *sheet, int const clear_flags)
{
	GtkObject *obj;
	CmdClear *me;
	gboolean trouble;

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
		me->paste_flags |= PASTE_VALUES | PASTE_FORMULAS;
	if (clear_flags & CLEAR_FORMATS)
		me->paste_flags |= PASTE_FORMATS;
	if (clear_flags & CLEAR_COMMENTS)
		g_warning ("Deleted comments can not be restored yet");

	/* TODO : Something more descriptive ? maybe the range name */
	me->parent.cmd_descriptor = g_strdup (_("Clear"));

	trouble = cmd_clear_redo (GNUMERIC_COMMAND(me), context);

	/* Register the command object */
	return command_push_undo (sheet->workbook, obj, trouble);
}

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
	 *
	 * Maybe queue it as 2 different commands, as a clear in one book and
	 * a paste  in the other.    This is not symetric though.  What happens to the
	 * cells in the original sheet that now reference the cells in the other.
	 * When do they reset to the original ?  Probably when the clear in the original
	 * is undone.
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
	CmdRenameSheet *me = CMD_RENAME_SHEET(cmd);

	g_return_val_if_fail (me != NULL, TRUE);

	return !workbook_rename_sheet (me->wb, me->new_name, me->old_name);
}

static gboolean
cmd_rename_sheet_redo (GnumericCommand *cmd, CommandContext *context)
{
	CmdRenameSheet *me = CMD_RENAME_SHEET(cmd);

	g_return_val_if_fail (me != NULL, TRUE);

	return !workbook_rename_sheet (me->wb, me->old_name, me->new_name);
}
static void
cmd_rename_sheet_destroy (GtkObject *cmd)
{
	CmdRenameSheet *me = CMD_RENAME_SHEET(cmd);

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
	gboolean trouble;

	g_return_val_if_fail (wb != NULL, TRUE);

	obj = gtk_type_new (CMD_RENAME_SHEET_TYPE);
	me = CMD_RENAME_SHEET (obj);

	/* Store the specs for the object */
	me->wb = wb;
	me->old_name = g_strdup (old_name);
	me->new_name = g_strdup (new_name);

	me->parent.cmd_descriptor = 
	    g_strdup_printf (_("Rename sheet '%s' '%s'"), old_name, new_name);

	trouble = cmd_rename_sheet_redo (GNUMERIC_COMMAND(me), context);

	/* Register the command object */
	return command_push_undo (wb, obj, trouble);
}

/******************************************************************/

#define CMD_SET_DATE_TIME_TYPE        (cmd_set_date_time_get_type ())
#define CMD_SET_DATE_TIME(o)          (GTK_CHECK_CAST ((o), CMD_SET_DATE_TIME_TYPE, CmdSetDateTime))

typedef struct
{
	GnumericCommand parent;

	gboolean	 is_date;
	EvalPosition	 pos;
	gchar		*contents;
} CmdSetDateTime;

GNUMERIC_MAKE_COMMAND (CmdSetDateTime, cmd_set_date_time);

static gboolean
cmd_set_date_time_undo (GnumericCommand *cmd, CommandContext *context)
{
	CmdSetDateTime *me = CMD_SET_DATE_TIME(cmd);
	Cell *cell;

	g_return_val_if_fail (me != NULL, TRUE);

	/* Get the cell */
	cell = sheet_cell_fetch (me->pos.sheet, me->pos.eval.col, me->pos.eval.row);

	g_return_val_if_fail (cell != NULL, TRUE);

	/* Restore the old value (possibly empty) */
	if (me->contents != NULL) {
		cell_set_text (cell, me->contents);
		g_free (me->contents);
		me->contents = NULL;
	} else
		cell_set_value (cell, value_new_empty ());

	sheet_load_cell_val (me->pos.sheet);
	return FALSE;
}

static gboolean
cmd_set_date_time_redo (GnumericCommand *cmd, CommandContext *context)
{
	CmdSetDateTime *me = CMD_SET_DATE_TIME(cmd);
	Value *v;
	Cell *cell;
	char const * prefered_format;

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->contents == NULL, TRUE);

	if (me->is_date) {
		v = value_new_int (datetime_timet_to_serial (time (NULL)));

		/* FIXME : the '>' prefix is intended to give the translators
		 * a change to provide a locale specific date format.
		 * This is ugly because the format may not show up in the
		 * list of date formats, and will be marked custom
		 */
		prefered_format = _(">mm/dd/yyyy");
	} else {
		v = value_new_float (datetime_timet_to_seconds (time (NULL)) / (24.0 * 60 * 60));

		/* FIXME : See comment above */
		prefered_format = _(">hh:mm");
	}

	/* Get the cell (creating it if needed) */
	cell = sheet_cell_fetch (me->pos.sheet, me->pos.eval.col, me->pos.eval.row);

	/* Ensure that we are not breaking part of an array */
	if (cell->parsed_node != NULL && cell->parsed_node->oper == OPER_ARRAY &&
	    (cell->parsed_node->u.array.rows != 1 ||
	     cell->parsed_node->u.array.cols != 1)) {
		gnumeric_error_splits_array (context);
		return TRUE;
	}

	/* Save contents */
	me->contents = (cell->value) ? cell_get_text (cell) : NULL;

	cell_set_value (cell, v);
	cell_set_format (cell, prefered_format+1);
	workbook_recalc (me->pos.sheet->workbook);
	sheet_load_cell_val (me->pos.sheet);

	return FALSE;
}
static void
cmd_set_date_time_destroy (GtkObject *cmd)
{
	CmdSetDateTime *me = CMD_SET_DATE_TIME(cmd);

	if (me->contents) {
		g_free (me->contents);
		me->contents = NULL;
	}
	gnumeric_command_destroy (cmd);
}

gboolean
cmd_set_date_time (CommandContext *context, gboolean is_date,
		   Sheet *sheet, int col, int row)
{
	GtkObject *obj;
	CmdSetDateTime *me;
	gboolean trouble;

	g_return_val_if_fail (sheet != NULL, TRUE);

	obj = gtk_type_new (CMD_SET_DATE_TIME_TYPE);
	me = CMD_SET_DATE_TIME (obj);

	/* Store the specs for the object */
	me->pos.sheet = sheet;
	me->pos.eval.col = col;
	me->pos.eval.row = row;
	me->is_date = is_date;
	me->contents = NULL;

	me->parent.cmd_descriptor =
	    g_strdup_printf (is_date
			     ? _("Setting current date in %s")
			     : _("Setting current time in %s"),
			     cell_name(col, row));

	trouble = cmd_set_date_time_redo (GNUMERIC_COMMAND(me), context);

	/* Register the command object */
	return command_push_undo (sheet->workbook, obj, trouble);
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
	CmdResizeRowCol *me = CMD_RESIZE_ROW_COL(cmd);

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
	CmdResizeRowCol *me = CMD_RESIZE_ROW_COL(cmd);

	g_return_val_if_fail (me != NULL, TRUE);
	g_return_val_if_fail (me->sizes == NULL, TRUE);

	me->sizes = sheet_save_row_col_sizes (me->sheet, me->is_col,
					      me->index, 1);
	return FALSE;
}
static void
cmd_resize_row_col_destroy (GtkObject *cmd)
{
	CmdResizeRowCol *me = CMD_RESIZE_ROW_COL(cmd);

	if (me->sizes) {
		g_free (me->sizes);
		me->sizes = NULL;
	}
	gnumeric_command_destroy (cmd);
}

gboolean
cmd_resize_row_col (CommandContext *context, gboolean is_col,
		   Sheet *sheet, int index)
{
	GtkObject *obj;
	CmdResizeRowCol *me;
	gboolean trouble;

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

	trouble = cmd_resize_row_col_redo (GNUMERIC_COMMAND(me), context);

	/* TODO :
	 * - Patch into manual and auto resizing 
	 * - store the selected sized,
	 *
	 * There is something odd about the way row/col resize is handled currently.
	 * The item-bar sends a signal to all the sheet-views each of which sets
	 * the size for the sheet.
	 */

	/* Register the command object */
	return command_push_undo (sheet->workbook, obj, trouble);
}

/******************************************************************/
/* TODO : Make a list of commands that should have undo support that dont
 *        even have stubs
 * - Autofill
 * - Array formula creation.
 * - Sorting (jpr is working on this ?)
 */

#include <gnumeric-config.h>
#include "gnumeric.h"
#include "gutils.h"
#include "workbook-control-component-priv.h"
#include <gal/util/e-util.h>

static char *
wbcc_get_password (CommandContext *cc, char const* msg) {}

static void
wbcc_progress_set (CommandContext *cc, gfloat val) {}

static void
wbcc_progress_message_set (CommandContext *cc, gchar const *msg) {}

static void
wbcc_error_system (CommandContext *cc, char const *msg)
{
	/* FIXME: Set exception */
	g_warning (msg);
}
static void
wbcc_error_plugin (CommandContext *cc, char const *msg)
{
	/* FIXME: Set exception */
	g_warning (msg);
}
static void
wbcc_error_read (CommandContext *cc, char const *msg)
{
	/* FIXME: Set exception */
	g_warning (msg);
}

static void
wbcc_error_save (CommandContext *cc, char const *msg)
{
	/* FIXME: Set exception */
	g_warning (msg);
}

static void
wbcc_error_invalid (CommandContext *cc, char const *msg, char const * value)
{
	/* FIXME: Set exception */
	char *buf = g_strconcat (msg, " : ", value, NULL);
	g_warning (buf);
	g_free (buf);
}

/* We inherit the implementation of error.splits_array
 * from WorkbookControlGUI */

static void
wbcc_error_error_info (CommandContext *context,
                       ErrorInfo *error)
{
	/* FIXME: Set exception */
	error_info_print (error);

}

static WorkbookControl *
wbcc_control_new (WorkbookControl *wbc, WorkbookView *wbv, Workbook *wb)
{
	return workbook_control_component_new (wbv, wb);
}

static void
wbcc_init_state (WorkbookControl *wbc) {}

static void
wbcc_title_set (WorkbookControl *wbc, char const *title) {}

static void
wbcc_format_feedback (WorkbookControl *wbc) {}

static void
wbcc_zoom_feedback (WorkbookControl *wbc) {}

static void
wbcc_edit_line_set (WorkbookControl *wbc, char const *text) {}

static void
wbcc_edit_selection_descr_set (WorkbookControl *wbc, char const *text) {}

static void
wbcc_auto_expr_value (WorkbookControl *wbc) {}

static void
wbcc_sheet_focus (WorkbookControl *wbc, Sheet *sheet)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	SheetControlGUI *scg;
	int i = wbcg_sheet_to_page_index (wbcg, sheet, &scg);

	/* A sheet added in another view may not yet have a view */
	if (i >= 0) {
		gtk_notebook_set_page (wbcg->notebook, i);
	}
}

static void
wbcc_undo_redo_clear (WorkbookControl *wbc, gboolean is_undo) {}

static void
wbcc_undo_redo_truncate (WorkbookControl *wbc, int n, gboolean is_undo) {}

static void
wbcc_undo_redo_pop (WorkbookControl *wbc, gboolean is_undo) {}

static void
wbcc_undo_redo_push (WorkbookControl *wbc,
		     char const *text, gboolean is_undo) {}

static void
wbcc_undo_redo_labels (WorkbookControl *wbc,
		       char const *undo, char const *redo) {}

static void
wbcc_menu_state_update (WorkbookControl *wbc, Sheet const *sheet, int flags) {}

static void
wbcc_menu_state_sheet_prefs (WorkbookControl *wbc, Sheet const *sheet) {}

static void
wbcc_menu_state_sheet_count (WorkbookControl *wbc) {}

static void
wbcc_menu_state_sensitivity (WorkbookControl *wbc, gboolean sensitive) {}

/* FIXME: Can this be made to work? */
static gboolean
wbcc_claim_selection (WorkbookControl *wbc)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	return gtk_selection_owner_set (GTK_WIDGET (wbcg->table),
					GDK_SELECTION_PRIMARY,
					GDK_CURRENT_TIME);
}

static int
wbcc_validation_msg (WorkbookControl *wbc, ValidationStyle v,
		     char const *title, char const *msg)
{}

static void
workbook_control_component_init (WorkbookControlComponent *wbcc,
				 WorkbookView *optional_view,
				 Workbook *optional_wb)
{
	static GtkTargetEntry const drag_types[] = {
		{ (char *)"text/uri-list", 0, 0 }
	};
	WorkbookControlGUI *wbcg = WORKBOOK_CONTROL_GUI (wbcc);

	wbcg->table    = gtk_table_new (0, 0, 0);
	wbcg->notebook = NULL;
	wbcg->updating_ui = FALSE;

	workbook_control_set_view (&wbcg->wb_control, optional_view, optional_wb);
	
	/* We are not in edit mode */
	wbcg->editing = FALSE;
	wbcg->editing_sheet = NULL;
	wbcg->editing_cell = NULL;
	wbcg->rangesel = NULL;

	/* FIXME: Insert appropriate lifecycle here, and signal handlers
	 * like for wbcg.  */
}

static void
workbook_control_component_ctor_class (GObjectClass *object_class)
{
	WorkbookControlClass *wbc_class
		= WORKBOOK_CONTROL_CLASS (object_class);

	g_return_if_fail (wbc_class != NULL);
	wbc_class->context_class.get_password = wbcc_get_password;
	wbc_class->context_class.progress_set = wbcc_progress_set;
	wbc_class->context_class.progress_message_set
		= wbcc_progress_message_set;

	wbc_class->context_class.error.system	= wbcc_error_system;
	wbc_class->context_class.error.plugin	= wbcc_error_plugin;
	wbc_class->context_class.error.read	= wbcc_error_read;
	wbc_class->context_class.error.save	= wbcc_error_save;
	wbc_class->context_class.error.invalid	= wbcc_error_invalid;
	/* wbc_class->context_class.error.splits_array inherited from wbcg */
	wbc_class->context_class.error.error_info  = wbcc_error_error_info;

	wbc_class->control_new	      = wbcc_control_new;
	wbc_class->init_state	      = wbcc_init_state;
	wbc_class->title_set	      = wbcc_title_set;
	/* wbc_class->prefs_update inherited from wbcg */
	wbc_class->format_feedback    = wbcc_format_feedback;
	wbc_class->zoom_feedback      = wbcc_zoom_feedback;
	wbc_class->edit_line_set      = wbcc_edit_line_set;
	wbc_class->selection_descr_set = wbcc_edit_selection_descr_set;
	wbc_class->auto_expr_value    = wbcc_auto_expr_value;

	/* wbc_class->sheet.add inherited from wbcg */
	/* wbc_class->sheet.remove inherited from wbcg */
	/* wbc_class->sheet.rename inherited from wbcg */
	wbc_class->sheet.focus	      = wbcc_sheet_focus;
	/* wbc_class->sheet.move inherited from wbcg */
	/* wbc_class->sheet.remove_all inherited from wbcg */

	wbc_class->undo_redo.clear    = wbcc_undo_redo_clear;
	wbc_class->undo_redo.truncate = wbcc_undo_redo_truncate;
	wbc_class->undo_redo.pop      = wbcc_undo_redo_pop;
	wbc_class->undo_redo.push     = wbcc_undo_redo_push;
	wbc_class->undo_redo.labels   = wbcc_undo_redo_labels;
	wbc_class->menu_state.update  = wbcc_menu_state_update;
	wbc_class->menu_state.sheet_prefs = wbcc_menu_state_sheet_prefs;
	wbc_class->menu_state.sensitivity = wbcc_menu_state_sensitivity;
	wbc_class->menu_state.sheet_count = wbcc_menu_state_sheet_count;

	wbc_class->claim_selection       = wbcc_claim_selection;
	/* wbc_class->paste_from_selection inherited from wbcg */
	wbc_class->validation_msg	 = wbcc_validation_msg;
}

E_MAKE_TYPE(workbook_control_component, "WorkbookControlComponent",
	    WorkbookControlComponent,
	    workbook_control_component_ctor_class, NULL,
	    WORKBOOK_CONTROL_GUI_TYPE);

WorkbookControl *
workbook_control_component_new (WorkbookView *optional_view, Workbook *wb)
{
	WorkbookControlComponent *wbcc;
	WorkbookControl *wbc;

	wbcc = g_object_new (workbook_control_component_get_type (), NULL);
	wbc  = WORKBOOK_CONTROL (wbcc);
	workbook_control_component_init (wbcc, optional_view, wb);
	workbook_control_init_state (wbc);

	return wbc;
}

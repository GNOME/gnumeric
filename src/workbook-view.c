#include <config.h>
#include "workbook-view.h"
#include "command-context-impl.h"
#include "workbook.h"
#include "gnumeric-util.h"

struct GUI_CmdContext
{
	CmdContext	base;
	Workbook	*wb;
};

static struct GUI_CmdContext *
command_context_gui_cast (CmdContext *context)
{
	g_return_val_if_fail (context != NULL, NULL);
	g_return_val_if_fail (context->type == CMD_CONTEXT_GUI, NULL);

	return (struct GUI_CmdContext *)context;
}

static void
gui_plugin_problem (CmdContext *context, char const * const mesg)
{
	struct GUI_CmdContext *gui = command_context_gui_cast (context);
	if (gui)
		gnumeric_notice (gui->wb, GNOME_MESSAGE_BOX_ERROR, mesg);
}

static void
gui_splits_array (CmdContext *context)
{
	struct GUI_CmdContext *gui = command_context_gui_cast (context);
	if (gui)
		gnumeric_notice (gui->wb, GNOME_MESSAGE_BOX_ERROR,
				 _("You cannot change part of an array."));
}

CmdContext *
command_context_gui (Workbook *wb)
{
	static gboolean needs_init = TRUE;
	static GnmCmdcontext_vtbl	vtbl;
	static struct GUI_CmdContext	context;
	if (needs_init) {
		command_context_vtbl_init (&vtbl);
		vtbl.plugin_problem = &gui_plugin_problem;
		vtbl.splits_array = &gui_splits_array;
		context.base.vtbl = &vtbl;
		context.base.type = CMD_CONTEXT_GUI;
	}

	/* WARNING : When things start to get threaded we will need to
	 * rethink this.  It may also cause problems with nested calls
	 * from one workbook to another.  However it is fine as a 1st cut.
	 * Later we can move the context into the workbook-view.
	 */
	context.wb = wb;
	return &context.base;
}

/* enable/disable paste/paste_special
 * 0 = both disabled
 * 1 = paste enabled
 * 2 = both enabled
 */
void
workbook_view_set_paste_state (Workbook *wb, int const state)
{
	g_return_if_fail (wb != NULL);
	g_return_if_fail (state >= 0);
	g_return_if_fail (state <= 2);

#ifndef ENABLE_BONOBO
	gtk_widget_set_sensitive (wb->menu_item_paste, state > 0);
	gtk_widget_set_sensitive (wb->menu_item_paste_special, state > 1);
#else
	/* gnome_ui_handler_menu_set_sensitivity (); */
#endif
}

void
workbook_view_set_undo_redo_state (Workbook const * const wb,
				   gboolean const has_undos,
				   gboolean const has_redos)
{
	g_return_if_fail (wb != NULL);

#ifndef ENABLE_BONOBO
	gtk_widget_set_sensitive (wb->menu_item_undo, has_undos);
	gtk_widget_set_sensitive (wb->menu_item_redo, has_redos);
#else
	/* gnome_ui_handler_menu_set_sensitivity (); */
#endif
}

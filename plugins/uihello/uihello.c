/*
 * uihello.c: sample plugin using "ui" service
 *
 * Author: Zbigniew Chyla (cyba@gnome.pl)
 */

#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include <glib.h>

#include <workbook-control-gui.h>
#include <gui-util.h>

static void
hello_message (GnmAction const *action, WorkbookControl *wbc)
{
	char *msg = g_strdup_printf (
		_("This is message from the \"%s\" plugin."),
		gnm_plugin_get_name (PLUGIN));
	gnumeric_notice (wbcg_toplevel (WORKBOOK_CONTROL_GUI (wbc)), GTK_MESSAGE_INFO, msg);
	g_free (msg);
}

ModulePluginUIActions const hello_ui_actions[] = {
	{ "HelloWorld", hello_message},
	{ NULL }
};

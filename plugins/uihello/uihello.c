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
#include <gnm-plugin.h>
#include <goffice/app/go-plugin.h>
#include <goffice/app/module-plugin-defs.h>

GOFFICE_MODULE_PLUGIN_INFO_DECL(GNUMERIC_VERSION);

static void
hello_message (GnmAction const *action, WorkbookControl *wbc)
{
	char *msg = g_strdup_printf (
		_("This is message from the \"%s\" plugin."),
		gnm_plugin_get_name (PLUGIN));
	go_gtk_notice_dialog (wbcg_toplevel (WORKBOOK_CONTROL_GUI (wbc)), GTK_MESSAGE_INFO, msg);
	g_free (msg);
}

ModulePluginUIActions const hello_ui_actions[] = {
	{ "HelloWorld", hello_message},
	{ NULL }
};

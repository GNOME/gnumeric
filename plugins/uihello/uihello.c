/*
 * uihello.c: sample plugin using "ui" service
 *
 * Author: Zbigniew Chyla (cyba@gnome.pl)
 */

#include <gnumeric-config.h>
#include <glib.h>
#include <libgnome/gnome-i18n.h>

#include <workbook-control-gui.h>
#include <gui-util.h>
#include <plugin.h>
#include <module-plugin-defs.h>

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

static void
hello_message (WorkbookControlGUI *wbcg)
{
	char *msg;

	msg = g_strdup_printf (
		_("This is message from \"%s\" plugin."),
		gnm_plugin_get_name (PLUGIN));
	gnumeric_notice (wbcg, GTK_MESSAGE_INFO, msg);
	g_free (msg);
}

const ModulePluginUIVerbInfo hello_ui_verbs[] = {
	{"HelloWorld", hello_message},
	{NULL}
};

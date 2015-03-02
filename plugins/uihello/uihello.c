/*
 * uihello.c: sample plugin using "ui" service
 *
 * Author: Zbigniew Chyla (cyba@gnome.pl)
 */

#include <gnumeric-config.h>
#include <wbc-gtk.h>
#include <gui-util.h>
#include <gnm-plugin.h>
#include <glib/gi18n-lib.h>

GNM_PLUGIN_MODULE_HEADER;

static GOPlugin *uihello_plugin;
G_MODULE_EXPORT void
go_plugin_init (GOPlugin *plugin, GOCmdContext *cc)
{
	uihello_plugin = plugin;
}

G_MODULE_EXPORT void
go_plugin_shutdown (GOPlugin *plugin, GOCmdContext *cc)
{
	uihello_plugin = NULL;
}

static void
hello_message (GnmAction const *action, WorkbookControl *wbc)
{
	char *msg = g_strdup_printf (
		_("This is message from the \"%s\" plugin."),
		go_plugin_get_name (uihello_plugin));
	go_gtk_notice_dialog (wbcg_toplevel (WBC_GTK (wbc)), GTK_MESSAGE_INFO,
			      "%s", msg);
	g_free (msg);
}

GnmModulePluginUIActions const hello_ui_actions[] = {
	{ "HelloMenu", NULL },
	{ "HelloWorld", hello_message },
	{ NULL }
};

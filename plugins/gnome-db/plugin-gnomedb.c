#include <gnumeric-config.h>
#include <gnumeric.h>
#include <wbc-gtk.h>
#include <gui-util.h>
#include <gnm-plugin.h>
#include <glib/gi18n-lib.h>

GNM_PLUGIN_MODULE_HEADER;

static void
view_data_sources (GnmAction const *action, WorkbookControl *wbc)
{
	char *argv[2];

	argv[0] = (char *) "gnome-database-properties-4.0";
	argv[1] = NULL;
	if (!g_spawn_async (NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL)) {
		char *msg = g_strdup_printf (
			_("Could not run GNOME database configuration tool ('%s')"),
			argv[0]);
		go_gtk_notice_dialog (wbcg_toplevel (WBC_GTK (wbc)),
				      GTK_MESSAGE_INFO,  "%s", msg);
		g_free (msg);
	}
}

GnmModulePluginUIActions const gnome_db_ui_actions[] = {
	{"ViewDataSources", view_data_sources},
	{NULL}
};

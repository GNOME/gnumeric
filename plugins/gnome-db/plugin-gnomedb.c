#include <gnumeric-config.h>
#include <wbc-gtk.h>
#include <gui-util.h>
#include <gnm-plugin.h>
#include <glib/gi18n-lib.h>

GNM_PLUGIN_MODULE_HEADER;

static void
view_data_sources (GnmAction const *action, WorkbookControl *wbc)
{
	char *argv[2];

	/* run gnome-database-properties config tool */
	argv[0] = (char *) "gnome-database-properties";
	argv[1] = NULL;

	if (!g_spawn_async (NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
			    NULL, NULL, NULL, NULL))
		go_gtk_notice_dialog (wbcg_toplevel (WBC_GTK (wbc)),
				 GTK_MESSAGE_INFO, 
				 _("Could not run GNOME database configuration tool"));
}

ModulePluginUIActions const gnome_db_ui_actions[] = {
	{"ViewDataSources", view_data_sources},
	{NULL}
};

#ifndef GO_PLUGIN_H
#define GO_PLUGIN_H

#include <goffice/app/goffice-app.h>
#include <glib-object.h>

/*
 * Use "#define PLUGIN_DEBUG x" to enable some plugin related debugging
 * messages.
#undef PLUGIN_DEBUG
 * Define PLUGIN_ALWAYS_LOAD to disable loading on demand feature
 */

#define GNM_PLUGIN_TYPE        (gnm_plugin_get_type ())
#define GNM_PLUGIN(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_PLUGIN_TYPE, GOPlugin))
#define IS_GNM_PLUGIN(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_PLUGIN_TYPE))

GType gnm_plugin_get_type (void);

void         gnm_plugin_activate (GOPlugin *pinfo, ErrorInfo **ret_error);
void         gnm_plugin_deactivate (GOPlugin *pinfo, ErrorInfo **ret_error);
gboolean     gnm_plugin_is_active (GOPlugin *pinfo);
gboolean     gnm_plugin_can_deactivate (GOPlugin *pinfo);
void         gnm_plugin_load_service (GOPlugin *pinfo, GOPluginService *service, ErrorInfo **ret_error);
void         gnm_plugin_unload_service (GOPlugin *pinfo, GOPluginService *service, ErrorInfo **ret_error);
gboolean     gnm_plugin_is_loaded (GOPlugin *pinfo);
void         gnm_plugin_use_ref (GOPlugin *pinfo);
void         gnm_plugin_use_unref (GOPlugin *pinfo);

char const  *gnm_plugin_get_dir_name (GOPlugin *pinfo);
char const  *gnm_plugin_get_id (GOPlugin *pinfo);
char const  *gnm_plugin_get_name (GOPlugin *pinfo);
char const  *gnm_plugin_get_description (GOPlugin *pinfo);
char const  *gnm_plugin_get_textdomain (GOPlugin *pinfo);
GSList      *gnm_plugin_get_dependencies_ids (GOPlugin *pinfo);
GSList      *gnm_plugin_get_services (GOPlugin *pinfo);

/*
 *
 */

void	go_plugins_init	    (GOCmdContext *context,
			     GSList const *known_states,
			     GSList const *active_plugins,
			     GSList *plugin_dirs,
			     gboolean activate_new_plugins,
			     GType  default_loader_type);
GSList *go_plugins_shutdown (void);

void         plugins_register_loader (const gchar *id_str, GOPluginService *service);
void         plugins_unregister_loader (const gchar *id_str);
GOPlugin   *plugins_get_plugin_by_id (const gchar *plugin_id);
GSList      *plugins_get_available_plugins (void);
GSList      *plugins_get_active_plugins (void);
void         plugins_rescan (ErrorInfo **ret_error, GSList **ret_new_plugins);
void         plugin_db_mark_plugin_for_deactivation (GOPlugin *pinfo, gboolean mark);
gboolean     plugin_db_is_plugin_marked_for_deactivation (GOPlugin *pinfo);
void         plugin_db_activate_plugin_list (GSList *plugins, ErrorInfo **ret_error);
void         plugin_db_deactivate_plugin_list (GSList *plugins, ErrorInfo **ret_error);

void plugin_message (gint level, const gchar *format, ...) G_GNUC_PRINTF (2, 3);

#endif /* GO_PLUGIN_H */

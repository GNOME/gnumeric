#ifndef GNUMERIC_PLUGIN_H
#define GNUMERIC_PLUGIN_H

#include <sys/types.h>
#include <gnome-xml/tree.h>
#include <gtk/gtktypeutils.h>
#include "gnumeric.h"
#include "error-info.h"
#include "gutils.h"

/*
 * Use "#define PLUGIN_DEBUG 0" to enable some plugin related debugging
 * messages.
#undef PLUGIN_DEBUG
 * Use "#define PLUGIN_ALWAYS_LOAD" to force loading plugin at activation
 * time.
#undef PLUGIN_ALWAYS_LOAD
*/

typedef struct _PluginInfo PluginInfo;
typedef struct _PluginService PluginService;

struct _GnumericPluginLoader;
struct _PluginServicesData;

typedef enum {
	DEPENDENCY_ACTIVATE,
	DEPENDENCY_LOAD,
	DEPENDENCY_LAST
} PluginDependencyType;

void         plugins_init (CommandContext *context);
void         plugins_shutdown (void);

GList       *gnumeric_extra_plugin_dirs (void);

typedef GtkType (*PluginLoaderGetTypeCallback) (gpointer callback_data, ErrorInfo **ret_error);

void         plugin_loader_register_type (const gchar *id_str, GtkType loader_type);
void         plugin_loader_register_id_only (const gchar *id_str, PluginLoaderGetTypeCallback callback,
                                             gpointer callback_data);
GtkType      plugin_loader_get_type_by_id (const gchar *id_str, ErrorInfo **ret_error);
gboolean     plugin_loader_is_available_by_id (const gchar *id_str);

void         plugin_info_free (PluginInfo *pinfo);
void         activate_plugin (PluginInfo *pinfo, ErrorInfo **ret_error);
void         deactivate_plugin (PluginInfo *pinfo, ErrorInfo **ret_error);
gboolean     plugin_can_deactivate (PluginInfo *pinfo);
void         plugin_load_service (PluginInfo *pinfo, PluginService *service, ErrorInfo **ret_error);
void         plugin_unload_service (PluginInfo *pinfo, PluginService *service, ErrorInfo **ret_error);
void         plugin_load_dependencies (PluginInfo *pinfo, ErrorInfo **ret_error);
void         plugin_inc_dependants (PluginInfo *pinfo, PluginDependencyType dep_type);
void         plugin_dec_dependants (PluginInfo *pinfo, PluginDependencyType dep_type);
void         plugin_dependencies_inc_dependants (PluginInfo *pinfo, PluginDependencyType dep_type);
void         plugin_dependencies_dec_dependants (PluginInfo *pinfo, PluginDependencyType dep_type);

PluginInfo  *plugin_db_get_plugin_info_by_plugin_id (const gchar *plugin_id);
GList       *plugin_db_get_known_plugin_id_list (void);
GList       *plugin_db_get_available_plugin_info_list (void);
void         plugin_db_update_saved_active_plugin_id_list (void);
void         plugin_db_init (ErrorInfo **ret_error);
void         plugin_db_shutdown (ErrorInfo **ret_error);
void         plugin_db_activate_plugin_list (GList *plugins, ErrorInfo **ret_error);
void         plugin_db_deactivate_plugin_list (GList *plugins, ErrorInfo **ret_error);
void         plugin_db_mark_plugin_for_deactivation (PluginInfo *pinfo, gboolean mark);
gboolean     plugin_db_is_plugin_marked_for_deactivation (PluginInfo *pinfo);
/*
 * For all plugin_info_get_* functions below you should free returned data after use
 */
gchar       *plugin_info_get_dir_name (PluginInfo *pinfo);
gchar       *plugin_info_get_id (PluginInfo *pinfo);
gchar       *plugin_info_get_name (PluginInfo *pinfo);
gchar       *plugin_info_get_description (PluginInfo *pinfo);
gchar       *plugin_info_get_config_prefix (PluginInfo *pinfo);
gint         plugin_info_get_extra_info_list (PluginInfo *pinfo, GList **ret_keys_list, GList **ret_values_list);
gboolean     plugin_info_is_active (PluginInfo *pinfo);
const gchar *plugin_info_peek_dir_name (PluginInfo *pinfo);
const gchar *plugin_info_peek_id (PluginInfo *pinfo);
const gchar *plugin_info_peek_name (PluginInfo *pinfo);
const gchar *plugin_info_peek_description (PluginInfo *pinfo);
const gchar *plugin_info_peek_loader_type_str (PluginInfo *pinfo);
gboolean     plugin_info_provides_loader_by_type_str (PluginInfo *pinfo, const gchar *loader_type_str);
gboolean     plugin_info_is_loaded (PluginInfo *pinfo);
struct _PluginServicesData *plugin_info_peek_services_data (PluginInfo *pinfo);
struct _GnumericPluginLoader *plugin_info_get_loader (PluginInfo *pinfo);

void plugin_message (gint level, const gchar *format, ...);

#endif /* GNUMERIC_PLUGIN_H */

#ifndef GNUMERIC_PLUGIN_H
#define GNUMERIC_PLUGIN_H

#include <sys/types.h>
#include <gnome-xml/tree.h>
#include "gnumeric.h"
#include "error-info.h"
#include "gutils.h"

typedef struct _PluginInfo PluginInfo;

void         plugins_init (CommandContext *context);
void         plugins_shutdown (void);

GList       *gnumeric_extra_plugin_dirs (void);

PluginInfo  *plugin_info_read (const gchar *dir_name, xmlNodePtr tree, ErrorInfo **ret_error);
void         plugin_info_free (PluginInfo *pinfo);
void         activate_plugin (PluginInfo *pinfo, ErrorInfo **ret_error);
void         deactivate_plugin (PluginInfo *pinfo, ErrorInfo **ret_error);
gboolean     can_deactivate_plugin (PluginInfo *pinfo);
void         plugin_info_print (PluginInfo *pinfo);
GList       *plugin_info_list_read_for_dir (const gchar *dir_name, ErrorInfo **ret_error);
GList       *plugin_info_list_read_for_subdirs_of_dir (const gchar *dir_name, ErrorInfo **ret_error);
GList       *plugin_info_list_read_for_subdirs_of_dir_list (GList *dir_list, ErrorInfo **ret_error);
GList       *plugin_info_list_read_for_all_dirs (ErrorInfo **ret_error);

GList       *plugin_db_get_known_plugin_id_list (void);
void         plugin_db_extend_known_plugin_id_list (GList *extra_ids);
gboolean     plugin_db_is_known_plugin (const gchar *plugin_id);
GList       *plugin_db_get_available_plugin_info_list (ErrorInfo **ret_error);
PluginInfo  *plugin_db_get_plugin_info_by_plugin_id (const gchar *plugin_id);
GList       *plugin_db_get_saved_active_plugin_id_list (void);
void         plugin_db_update_saved_active_plugin_id_list (void);
void         plugin_db_extend_saved_active_plugin_id_list (GList *extra_ids);
gboolean     plugin_db_is_saved_active_plugin (const gchar *plugin_id);
void         plugin_db_init (ErrorInfo **ret_error);
void         plugin_db_shutdown (ErrorInfo **ret_error);
void         plugin_db_activate_saved_active_plugins (ErrorInfo **ret_error);
void         plugin_db_activate_plugin_list (GList *plugins, ErrorInfo **ret_error);
void         plugin_db_deactivate_plugin_list (GList *plugins, ErrorInfo **ret_error);

/*
 * For all plugin_info_get_* functions below you should free returned data after use
 */
gchar       *plugin_info_get_dir_name (PluginInfo *pinfo);
gchar       *plugin_info_get_id (PluginInfo *pinfo);
gchar       *plugin_info_get_name (PluginInfo *pinfo);
gchar       *plugin_info_get_description (PluginInfo *pinfo);
gint         plugin_info_get_extra_info_list (PluginInfo *pinfo, GList **ret_keys_list, GList **ret_values_list);
gboolean     plugin_info_is_active (PluginInfo *pinfo);
const gchar *plugin_info_peek_dir_name (PluginInfo *pinfo);
const gchar *plugin_info_peek_id (PluginInfo *pinfo);
const gchar *plugin_info_peek_name (PluginInfo *pinfo);
const gchar *plugin_info_peek_description (PluginInfo *pinfo);

/*
 * Three functions below should be defined by module plugins.
 * Every plugin should also define string gnumeric_plugin_version, matching
 * version of gnumeric it's compiled for.
 * (gchar gnumeric_plugin_version[] = GNUMERIC_VERSION;)
 *
 */

gboolean init_plugin (PluginInfo *, ErrorInfo **ret_error);
gboolean cleanup_plugin (PluginInfo *);
gboolean can_deactivate_plugin (PluginInfo *);

#endif /* GNUMERIC_PLUGIN_H */

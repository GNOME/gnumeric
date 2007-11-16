/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_PLUGIN_H_
# define _GNM_PLUGIN_H_

#include <gnumeric.h>
#include <goffice/app/goffice-app.h>
#include <goffice/app/go-plugin.h>
#include <goffice/app/module-plugin-defs.h>
#include <gmodule.h>
#include <libxml/tree.h>
#include <gsf/gsf.h>

G_BEGIN_DECLS

#define GNM_PLUGIN_SERVICE_FUNCTION_GROUP_TYPE  (plugin_service_function_group_get_type ())
#define GNM_PLUGIN_SERVICE_FUNCTION_GROUP(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_PLUGIN_SERVICE_FUNCTION_GROUP_TYPE, PluginServiceFunctionGroup))
#define IS_GNM_PLUGIN_SERVICE_FUNCTION_GROUP(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_PLUGIN_SERVICE_FUNCTION_GROUP_TYPE))

GType plugin_service_function_group_get_type (void);
typedef struct _PluginServiceFunctionGroup	PluginServiceFunctionGroup;
typedef struct {
	gboolean (*func_desc_load) (GOPluginService *service, char const *name,
				    GnmFuncDescriptor *res);
} PluginServiceFunctionGroupCallbacks;

#define GNM_PLUGIN_SERVICE_UI_TYPE  (plugin_service_ui_get_type ())
#define GNM_PLUGIN_SERVICE_UI(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_PLUGIN_SERVICE_UI_TYPE, PluginServiceUI))
#define IS_GNM_PLUGIN_SERVICE_UI(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_PLUGIN_SERVICE_UI_TYPE))

GType plugin_service_ui_get_type (void);
typedef struct _PluginServiceUI PluginServiceUI;
typedef struct {
	void (*plugin_func_exec_action) (
		GOPluginService *service, GnmAction const *action,
		WorkbookControl *wbc, ErrorInfo **ret_error);
} PluginServiceUICallbacks;

/* This type is intended for use with "ui" service.
 * Plugins should define arrays of structs of the form:
 * ModulePluginUIActions <service-id>_actions[] = { ... };
 */
typedef struct {
	char const *name;
	void (*handler) (GnmAction const *action, WorkbookControl *wbc);
} ModulePluginUIActions;

/**************************************************************************/
#define GNM_PLUGIN_MODULE_HEADER					\
G_MODULE_EXPORT GOPluginModuleDepend const go_plugin_depends [] = {	\
	{ "goffice",	GOFFICE_API_VERSION },				\
	{ "gnumeric",	GNM_VERSION_FULL }				\
};	\
G_MODULE_EXPORT GOPluginModuleHeader const go_plugin_header =  		\
	{ GOFFICE_MODULE_PLUGIN_MAGIC_NUMBER, G_N_ELEMENTS (go_plugin_depends) }

/**************************************************************************/

void gnm_plugins_init (GOCmdContext *c);

G_END_DECLS

#endif /* _GNM_PLUGIN_H_ */

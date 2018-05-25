#ifndef _GNM_PLUGIN_H_
# define _GNM_PLUGIN_H_

#include <gnumeric.h>
#include <goffice/goffice.h>
#include <goffice/app/module-plugin-defs.h>
#include <tools/gnm-solver.h>
#include <gmodule.h>

G_BEGIN_DECLS

#define GNM_PLUGIN_LOADER_MODULE_TYPE (gnm_plugin_loader_module_get_type ())
#define GNM_PLUGIN_LOADER_MODULE(o)  (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_PLUGIN_LOADER_MODULE_TYPE, GnmPluginLoaderModule))
GType gnm_plugin_loader_module_get_type (void);

#define GNM_PLUGIN_SERVICE_FUNCTION_GROUP_TYPE  (gnm_plugin_service_function_group_get_type ())
#define GNM_PLUGIN_SERVICE_FUNCTION_GROUP(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_PLUGIN_SERVICE_FUNCTION_GROUP_TYPE, GnmPluginServiceFunctionGroup))
#define GNM_IS_PLUGIN_SERVICE_FUNCTION_GROUP(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_PLUGIN_SERVICE_FUNCTION_GROUP_TYPE))

GType gnm_plugin_service_function_group_get_type (void);
typedef struct GnmPluginServiceFunctionGroup_	GnmPluginServiceFunctionGroup;
typedef struct {
	void (*load_stub) (GOPluginService *service, GnmFunc *func);
} GnmPluginServiceFunctionGroupCallbacks;

#define GNM_PLUGIN_SERVICE_UI_TYPE  (gnm_plugin_service_ui_get_type ())
#define GNM_PLUGIN_SERVICE_UI(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_PLUGIN_SERVICE_UI_TYPE, PluginServiceUI))
#define GNM_IS_PLUGIN_SERVICE_UI(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_PLUGIN_SERVICE_UI_TYPE))

GType gnm_plugin_service_ui_get_type (void);
typedef struct GnmPluginServiceUI_ PluginServiceUI;
typedef struct {
	void (*plugin_func_exec_action) (
		GOPluginService *service, GnmAction const *action,
		WorkbookControl *wbc, GOErrorInfo **ret_error);
} GnmPluginServiceUICallbacks;

/* This type is intended for use with "ui" service.
 * Plugins should define arrays of structs of the form:
 * GnmModulePluginUIActions <service-id>_actions[] = { ... };
 */
typedef struct {
	char const *name;
	void (*handler) (GnmAction const *action, WorkbookControl *wbc);
} GnmModulePluginUIActions;

#define GNM_PLUGIN_SERVICE_SOLVER_TYPE  (gnm_plugin_service_solver_get_type ())
#define GNM_PLUGIN_SERVICE_SOLVER(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_PLUGIN_SERVICE_SOLVER_TYPE, PluginServiceSolver))
#define GNM_IS_PLUGIN_SERVICE_SOLVER(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_PLUGIN_SERVICE_SOLVER_TYPE))

GType gnm_plugin_service_solver_get_type (void);
typedef struct GnmPluginServiceSolver_ PluginServiceSolver;
typedef struct {
	GnmSolverCreator creator;
	GnmSolverFactoryFunctional functional;
} GnmPluginServiceSolverCallbacks;

/**************************************************************************/
#define GNM_PLUGIN_MODULE_HEADER					\
G_MODULE_EXPORT GOPluginModuleDepend const go_plugin_depends [] = {	\
	{ "goffice",	GOFFICE_API_VERSION },				\
	{ "gnumeric",	GNM_VERSION_FULL }				\
};	\
G_MODULE_EXPORT GOPluginModuleHeader const go_plugin_header =		\
	{ GOFFICE_MODULE_PLUGIN_MAGIC_NUMBER, G_N_ELEMENTS (go_plugin_depends) }

/**************************************************************************/

void gnm_plugins_service_init (void);
void gnm_plugins_init (GOCmdContext *c);

G_END_DECLS

#endif /* _GNM_PLUGIN_H_ */

#ifndef GNUMERIC_MODULE_PLUGIN_DEFS_H
#define GNUMERIC_MODULE_PLUGIN_DEFS_H

#include <glib.h>
#include <plugin.h>
#include <func.h>
#include <workbook-control-gui.h>

/*
 * Every plugin should put somewhere a line with:
 * GNUMERIC_MODULE_PLUGIN_INFO_DECL;
 */
#define GNUMERIC_MODULE_PLUGIN_INFO_DECL     ModulePluginFileStruct plugin_file_struct = GNUMERIC_MODULE_PLUGIN_FILE_STRUCT_INITIALIZER

/* This type is intended for use with "function_group" service.
 * Plugins should define arrays of structs of the form:
 * ModulePluginFunctionInfo <service-id>_functions[] = { ... };
 */
typedef struct {
	gchar const *fn_name;
	gchar const *args;
	gchar const *arg_names;
	gchar const **help;
	FunctionArgs	 fn_args;
	FunctionNodes	 fn_nodes;
	FuncLinkHandle	 link;
	FuncUnlinkHandle unlink;
} ModulePluginFunctionInfo;

/* This type is intended for use with "ui" service.
 * Plugins should define arrays of structs of the form:
 * ModulePluginUIVerbInfo <service-id>_ui_verbs[] = { ... };
 */
typedef struct {
	char const *verb_name;
	void (*verb_func) (WorkbookControlGUI *wbcg);
} ModulePluginUIVerbInfo;

/* function executed to activate "general" service */
void     plugin_init_general (ErrorInfo **ret_error);
/* function executed to deactivate "general" service */
void     plugin_cleanup_general (ErrorInfo **ret_error);

/* optional function executed immediately after loading a plugin */
void      plugin_init (void);
/* optional function executed before unloading a plugin */
void      plugin_cleanup (void);


#ifdef PLUGIN_ID
static GnmPlugin *gnm_get_current_plugin (void)
{
	static GnmPlugin *plugin = NULL;
	if (plugin == NULL) plugin = plugins_get_plugin_by_id (PLUGIN_ID);
	return plugin;
}
#define PLUGIN (gnm_get_current_plugin ())
#endif


/* All fields in this structure are PRIVATE. */
typedef struct {
	guint32 magic_number;
	gchar gnumeric_plugin_version[64];
} ModulePluginFileStruct;
#define GNUMERIC_MODULE_PLUGIN_MAGIC_NUMBER             0x476e756d
#define GNUMERIC_MODULE_PLUGIN_FILE_STRUCT_INITIALIZER  {GNUMERIC_MODULE_PLUGIN_MAGIC_NUMBER, GNUMERIC_VERSION}


/* Use this macro for defining types inside plugins */
#define	PLUGIN_CLASS(name, prefix, class_init, instance_init, parent_type) \
GType \
prefix ## _get_type (void) \
{ \
	GType type = 0; \
	if (type == 0) { \
		static GTypeInfo const object_info = { \
			sizeof (name ## Class), \
			(GBaseInitFunc) NULL, \
			(GBaseFinalizeFunc) NULL, \
			(GClassInitFunc) class_init, \
			(GClassFinalizeFunc) NULL, \
			NULL,	/* class_data */ \
			sizeof (name), \
			0,	/* n_preallocs */ \
			(GInstanceInitFunc) instance_init, \
			NULL \
		}; \
		type = g_type_module_register_type ( \
			G_TYPE_MODULE (PLUGIN), parent_type, #name, \
			&object_info, 0); \
	} \
	return type; \
}

#endif /* GNUMERIC_MODULE_PLUGIN_DEFS_H */

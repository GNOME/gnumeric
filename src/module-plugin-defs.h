#ifndef GNUMERIC_MODULE_PLUGIN_DEFS_H
#define GNUMERIC_MODULE_PLUGIN_DEFS_H

#include <gui-gnumeric.h>	/* for wbcg typedef */
#include <plugin.h>
#include <func.h>

/*
 * Every plugin should put somewhere a line with:
 * GNUMERIC_MODULE_PLUGIN_INFO_DECL;
 */
#define GNUMERIC_MODULE_PLUGIN_INFO_DECL     ModulePluginFileStruct plugin_file_struct = GNUMERIC_MODULE_PLUGIN_FILE_STRUCT_INITIALIZER

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


/* All fields in this structure are PRIVATE. */
typedef struct {
	guint32 magic_number;
	gchar   version[64];
} ModulePluginFileStruct;

#define GNUMERIC_MODULE_PLUGIN_MAGIC_NUMBER             0x476e756d
#define GNUMERIC_MODULE_PLUGIN_FILE_STRUCT_INITIALIZER  {GNUMERIC_MODULE_PLUGIN_MAGIC_NUMBER, GNUMERIC_VERSION}

#endif /* GNUMERIC_MODULE_PLUGIN_DEFS_H */

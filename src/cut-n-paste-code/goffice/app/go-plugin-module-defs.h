#ifndef GO_PLUGIN_MODULE_DEFS_H
#define GO_PLUGIN_MODULE_DEFS_H

void plugin_init (void);    /* optional function executed immediately after loading a plugin */
void plugin_cleanup (void); /* optional function executed before unloading a plugin */

/*
 * Every plugin should put somewhere a line with:
 * GO_PLUGIN_MODULE_INFO_DECL;
 */
#define GO_PLUGIN_MODULE_INFO_DECL	\
    GOPluginModuleDescription go_plugin_module_description = GO_PLUGIN_MODULE_FILE_STRUCT_INITIALIZER

/* All fields in this structure are PRIVATE. */
typedef struct {
	guint32 magic_number;
	gchar   version[64];
} GOPluginModuleDescription;

#define GO_PLUGIN_MODULE_MAGIC_NUMBER		 0x476e756d
#define GO_PLUGIN_MODULE_FILE_STRUCT_INITIALIZER { GO_PLUGIN_MODULE_MAGIC_NUMBER, GNUMERIC_VERSION }

#endif /* GO_PLUGIN_MODULE_DEFS_H */

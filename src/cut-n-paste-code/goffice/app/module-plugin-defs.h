#ifndef GNUMERIC_MODULE_PLUGIN_DEFS_H
#define GNUMERIC_MODULE_PLUGIN_DEFS_H

#include <goffice/app/go-plugin.h>
#include <goffice/app/goffice-app.h>
#include <gmodule.h>

/*
 * Every plugin should put somewhere a line with:
 * GNUMERIC_MODULE_PLUGIN_INFO_DECL;
 */
#define GNUMERIC_MODULE_PLUGIN_INFO_DECL     ModulePluginFileStruct plugin_file_struct = GNUMERIC_MODULE_PLUGIN_FILE_STRUCT_INITIALIZER

void go_plugin_init	(GOPlugin *p, GOCmdContext *cc); /* optional, called after dlopen */
void go_plugin_shutdown	(GOPlugin *p, GOCmdContext *cc); /* optional, called before close */

#ifdef PLUGIN_ID

static GOPlugin *gnm_get_current_plugin (void)
{
	static GOPlugin *plugin = NULL;
	if (plugin == NULL) plugin = plugins_get_plugin_by_id (PLUGIN_ID);
	return plugin;
}
#define PLUGIN (gnm_get_current_plugin ())

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
			G_TYPE_MODULE (gnm_get_current_plugin ()), parent_type, #name, \
			&object_info, 0); \
	} \
	return type; \
}

#endif


/* All fields in this structure are PRIVATE. */
typedef struct {
	guint32 magic_number;
	gchar   version[64];
} ModulePluginFileStruct;

#define GNUMERIC_MODULE_PLUGIN_MAGIC_NUMBER             0x476e756d
#define GNUMERIC_MODULE_PLUGIN_FILE_STRUCT_INITIALIZER  {GNUMERIC_MODULE_PLUGIN_MAGIC_NUMBER, GNUMERIC_VERSION}

#endif /* GNUMERIC_MODULE_PLUGIN_DEFS_H */

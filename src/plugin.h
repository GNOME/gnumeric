#ifndef _PLUGIN_H
#define _PLUGIN_H

#include <gmodule.h>

struct PluginData
{
	GModule *handle;
	int     (*init_plugin)    (struct PluginData *);
	void    (*cleanup_plugin) (struct PluginData *);
	int     refcount;
	gchar   *title;
	
	/* filled in by plugin */
	void    *private;
};

typedef struct PluginData PluginData;

extern GList *plugin_list;

void           plugins_init          (void);
PluginData    *plugin_load           (gchar *filename);
void           plugin_unload         (PluginData *pd);
GtkWidget     *plugin_manager_new    (void);

#endif

#ifndef GNUMERIC_PLUGIN_H
#define GNUMERIC_PLUGIN_H

#include <gmodule.h>

struct PluginData
{
	GModule *handle;
	int     (*init_plugin)    (struct PluginData *);
	int     (*can_unload)     (struct PluginData *);
	void    (*cleanup_plugin) (struct PluginData *);
	gchar   *title;
	
	/* filled in by plugin */
	void    *private;
};

typedef struct PluginData PluginData;

extern GList *plugin_list;

void           plugins_init          (void);
PluginData    *plugin_load           (Workbook *wb, const gchar *filename);
void           plugin_unload         (Workbook *wb, PluginData *pd);
GtkWidget     *plugin_manager_new    (Workbook *wb);

#endif /* GNUMERIC_PLUGIN_H */

#ifndef GNUMERIC_PLUGIN_H
#define GNUMERIC_PLUGIN_H

/* Forward references for structures.  */
typedef struct _PluginData PluginData;

#include "sheet.h"
#include <gmodule.h>

struct _PluginData
{
	GModule *handle;
	int     (*init_plugin)    (PluginData *);
	int     (*can_unload)     (PluginData *);
	void    (*cleanup_plugin) (PluginData *);
	gchar   *title;
	
	/* filled in by plugin */
	void    *private;
};

extern GList *plugin_list;

/* Each plugin must have this one function */
extern int init_plugin (PluginData *pd);

void           plugins_init          (void);
PluginData    *plugin_load           (Workbook *wb, const gchar *filename);
void           plugin_unload         (Workbook *wb, PluginData *pd);
GtkWidget     *plugin_manager_new    (Workbook *wb);

#endif /* GNUMERIC_PLUGIN_H */

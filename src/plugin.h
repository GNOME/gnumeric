#ifndef GNUMERIC_PLUGIN_H
#define GNUMERIC_PLUGIN_H

/* Forward references for structures.  */
typedef struct _PluginData PluginData;

#include "sheet.h"
#include <gmodule.h>

struct _PluginData
{
	gchar   *file_name;
	GModule *handle;
	int     (*init_plugin)    (CmdContext *, PluginData *);
	int     (*can_unload)     (PluginData *);
	void    (*cleanup_plugin) (PluginData *);
	gchar   *title;
	
	/* filled in by plugin */
	void    *private_data;
};

extern GList *plugin_list;

/* Each plugin must have this one function */
extern int init_plugin (CmdContext *cmd, PluginData *pd);

void           plugins_init          (void);
PluginData    *plugin_load           (Workbook *wb, const gchar *filename);
void           plugin_unload         (Workbook *wb, PluginData *pd);
GtkWidget     *plugin_manager_new    (Workbook *wb);

gboolean       plugin_version_mismatch  (CmdContext *cmd, PluginData *pd,
					 char const * const plugin_version);

#endif /* GNUMERIC_PLUGIN_H */

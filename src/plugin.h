#ifndef GNUMERIC_PLUGIN_H
#define GNUMERIC_PLUGIN_H

/* Forward references for structures.  */
typedef struct _PluginData PluginData;

#include "sheet.h"
#include <gmodule.h>

typedef enum {
    PLUGIN_OK,
    PLUGIN_ERROR,	/* Display an error */
    PLUGIN_QUIET_ERROR /* Plugin has already displayed an error */
} PluginInitResult;

struct _PluginData
{
	gchar   *file_name;
	GModule *handle;

	PluginInitResult (*init_plugin)    (CommandContext *, PluginData *);
	int     (*can_unload)     (PluginData *);
	void    (*cleanup_plugin) (PluginData *);
	gchar   *title;
	
	/* filled in by plugin */
	void    *private_data;
};

extern GList *plugin_list;

/* Each plugin must have this one function */
extern PluginInitResult init_plugin (CommandContext *cmd, PluginData *pd);

void           plugins_init          (CommandContext *context);
PluginData    *plugin_load           (CommandContext *context,
				      const gchar *filename);
void           plugin_unload         (CommandContext *context, PluginData *pd);
GtkWidget     *plugin_manager_new    (Workbook *wb);

gboolean       plugin_version_mismatch  (CommandContext *cmd, PluginData *pd,
					 char const * const plugin_version);

#endif /* GNUMERIC_PLUGIN_H */

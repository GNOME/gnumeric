#ifndef GNUMERIC_PLUGIN_H
#define GNUMERIC_PLUGIN_H

/* Forward references for structures.  */
typedef struct _PluginData PluginData;

#include "gnumeric.h"
#include <gmodule.h>

typedef enum {
	PLUGIN_OK,
	PLUGIN_ERROR,	/* Display an error */
	PLUGIN_QUIET_ERROR /* Plugin has already displayed an error */
} PluginInitResult;

typedef PluginInitResult (*PluginInitFn) (CommandContext *, PluginData *);
typedef void             (*PluginCleanupFn) (PluginData *);
typedef int              (*PluginCanUnloadFn) (PluginData *);

extern GList *plugin_list;

/* Each plugin must have this one function */
extern PluginInitResult init_plugin (CommandContext *cmd, PluginData *pd);

void           plugins_init          (CommandContext *context);
PluginData    *plugin_load           (CommandContext *context,
				      const gchar *filename);
void           plugin_unload         (CommandContext *context, PluginData *pd);

gboolean       plugin_version_mismatch  (CommandContext *cmd, PluginData *pd,
					 char const * const plugin_version);

void           *plugin_data_set_user_data (PluginData *pd, void *user_data);
void           *plugin_data_get_user_data (PluginData *pd);

gboolean       plugin_data_init      (PluginData *pd, PluginCanUnloadFn can_unload_fn,
				      PluginCleanupFn cleanup_fn,
				      const gchar *title, const gchar *descr);

const gchar    *plugin_data_get_filename (PluginData *pd);
const gchar    *plugin_data_get_title    (PluginData *pd);
const gchar    *plugin_data_get_descr    (PluginData *pd);

#endif /* GNUMERIC_PLUGIN_H */

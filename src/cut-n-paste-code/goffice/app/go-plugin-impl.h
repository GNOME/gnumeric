#ifndef GO_PLUGIN_IMPL_H
#define GO_PLUGIN_IMPL_H

#include <gmodule.h>
#include <goffice/app/goffice-app.h>

/**
 * NOTE : Think about better names
 * TODO : init and cleanup should be given CommandContexts
 * to make things tidier
 */
void go_plugin_init	(GOPlugin *plugin);	/* optional function executed immediately after loading a plugin */
void go_plugin_cleanup	(GOPlugin *plugin);	/* optional function executed before unloading a plugin */

#endif /* GO_PLUGIN_IMPL_H */

#ifndef GNM_PERL_PLUGIN_LOADER_H
#define GNM_PERL_PLUGIN_LOADER_H

#include <glib-object.h>

GType gnm_perl_plugin_loader_get_type (void);
void  gnm_perl_plugin_loader_register_type (GTypeModule *module);

#endif /* GNM_PERL_PLUGIN_LOADER_H */

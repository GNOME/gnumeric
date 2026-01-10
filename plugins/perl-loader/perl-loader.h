#ifndef GNM_PERL_PLUGIN_LOADER_H_
#define GNM_PERL_PLUGIN_LOADER_H_

#include <glib-object.h>

GType gnm_perl_plugin_loader_get_type (void);
void  gnm_perl_plugin_loader_register_type (GTypeModule *module);

void gnm_perl_loader_free_later (gconstpointer data);

#endif /* GNM_PERL_PLUGIN_LOADER_H_ */

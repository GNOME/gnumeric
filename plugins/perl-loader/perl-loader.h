#ifndef PLUGIN_PERL_LOADER_H
#define PLUGIN_PERL_LOADER_H

#include <glib.h>

#define TYPE_GNM_PLUGIN_LOADER_PERL	(gnm_plugin_loader_perl_get_type ())
#define GNM_PLUGIN_LOADER_PERL(obj)	(G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_GNM_PLUGIN_LOADER_PERL, GnmPluginLoaderPerl))
#define IS_GNM_PLUGIN_LOADER_PERL(obj)	(G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_GNM_PLUGIN_LOADER_PERL))

GType gnm_plugin_loader_perl_get_type (void);

#endif /* PLUGIN_PERL_LOADER_H */

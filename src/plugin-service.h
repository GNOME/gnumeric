#ifndef GNUMERIC_PLUGIN_SERVICE_H
#define GNUMERIC_PLUGIN_SERVICE_H

#include <glib.h>
#include <gmodule.h>
#include <gnome-xml/tree.h>
#include <gal/util/e-xml-utils.h>
#include "gnumeric.h"
#include "file.h"
#include "func.h"
#include "error-info.h"
#include "plugin.h"

typedef struct _PluginServiceGeneral PluginServiceGeneral;
typedef struct _PluginServiceFileOpener PluginServiceFileOpener;
typedef struct _PluginServiceFileSaver PluginServiceFileSaver;
typedef struct _PluginServiceFunctionGroup PluginServiceFunctionGroup;
typedef struct _PluginServicePluginLoader PluginServicePluginLoader;
typedef struct _PluginServicesData PluginServicesData;

struct _PluginServiceGeneral {
	/* fields available after loading */
	void (*plugin_func_init) (PluginService *service, ErrorInfo **ret_error);
	gboolean (*plugin_func_can_deactivate) (PluginService *service);
	void (*plugin_func_cleanup) (PluginService *service, ErrorInfo **ret_error);
};

typedef struct _InputFilePattern InputFilePattern;
typedef struct _InputFileSaveInfo InputFileSaveInfo;

struct _PluginServiceFileOpener {
	gchar *id;
	gint priority;
	gboolean has_probe;
	gboolean can_open;
	gboolean can_import;
	gchar *description;
	GList *file_patterns;      /* list of InputFilePattern */
	InputFileSaveInfo *save_info;

	GnumFileOpener *opener;
	/* fields available after loading */
	gboolean (*plugin_func_file_probe) (GnumFileOpener const *fo, PluginService *service,
	                                    const gchar *file_name);
	void (*plugin_func_file_open) (GnumFileOpener const *fo, PluginService *service,
	                               IOContext *io_context, WorkbookView *wbv,
	                               const gchar *file_name);
};

struct _PluginServiceFileSaver {
	gchar *id;
	gchar *file_extension;
	FileFormatLevel format_level;
	gchar *description;

	GnumFileSaver *saver;
	/* fields available after loading */
	void  (*plugin_func_file_save) (GnumFileSaver const *fs, PluginService *service,
	                                IOContext *io_context, WorkbookView *wbv,
	                                const gchar *file_name);
};

struct _PluginServiceFunctionGroup {
	gchar *group_id;
	gchar *category_name, *translated_category_name;
	GList *function_name_list;

	FunctionCategory *category;
	/* fields available after loading */
	gboolean (*plugin_func_get_full_function_info) (PluginService *service,
	                                                gchar const *fn_name, gchar **args_ptr,
	                                                gchar **arg_names_ptr,
	                                                gchar ***help_ptr,
	                                                FunctionArgs **fn_args_ptr,
	                                                FunctionNodes **fn_nodes_ptr);
};

struct _PluginServicePluginLoader {
	gchar *loader_id;
	/* fields available after loading */
	GtkType (*plugin_func_get_loader_type) (PluginService *service,
	                                        ErrorInfo **ret_error);
};

typedef enum {
	PLUGIN_SERVICE_GENERAL,
	PLUGIN_SERVICE_FILE_OPENER,
	PLUGIN_SERVICE_FILE_SAVER,
	PLUGIN_SERVICE_FUNCTION_GROUP,
	PLUGIN_SERVICE_PLUGIN_LOADER,
	PLUGIN_SERVICE_LAST
} PluginServiceType;

struct _PluginService {
	gboolean is_active;
	gboolean is_loaded;
	PluginInfo *plugin;
	gpointer loader_data;
	PluginServiceType service_type;
	union {
		PluginServiceGeneral general;
		PluginServiceFileOpener file_opener;
		PluginServiceFileSaver file_saver;
		PluginServiceFunctionGroup function_group;
		PluginServicePluginLoader plugin_loader;
	} t;
};


PluginService *plugin_service_read (xmlNode *tree, ErrorInfo **ret_error);
void           plugin_service_free (PluginService *service);
void           plugin_service_set_plugin (PluginService *service, PluginInfo *plugin);
void           plugin_service_set_loader_data (PluginService *service, gpointer loader_data);
void           plugin_service_clear_loader_data (PluginService *service);
gpointer       plugin_service_get_loader_data (PluginService *service);
void           plugin_service_activate (PluginService *service, ErrorInfo **ret_error);
gboolean       plugin_service_can_deactivate (PluginService *service);
void           plugin_service_deactivate (PluginService *service, ErrorInfo **ret_error);

PluginServicesData *plugin_services_data_new (void);
void                plugin_services_data_free (PluginServicesData *services_data);

#endif /* GNUMERIC_PLUGIN_SERVICE_H */

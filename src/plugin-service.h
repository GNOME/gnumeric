#ifndef GNUMERIC_PLUGIN_SERVICE_H
#define GNUMERIC_PLUGIN_SERVICE_H

#include <glib.h>
#include <gmodule.h>
#include <libxml/tree.h>
#include "gnumeric.h"
#include "file.h"
#include "func.h"
#include "error-info.h"
#include "plugin.h"

#define GNM_PLUGIN_SERVICE_TYPE         (plugin_service_get_type ())
#define GNM_PLUGIN_SERVICE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_PLUGIN_SERVICE_TYPE, GnmPluginService))
#define IS_GNM_PLUGIN_SERVICE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_PLUGIN_SERVICE_TYPE))

GType plugin_service_get_type (void);

#define GNM_PLUGIN_SERVICE_GENERAL_TYPE  (plugin_service_general_get_type ())
#define GNM_PLUGIN_SERVICE_GENERAL(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_PLUGIN_SERVICE_GENERAL_TYPE, PluginServiceGeneral))
#define IS_GNM_PLUGIN_SERVICE_GENERAL(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_PLUGIN_SERVICE_GENERAL_TYPE))

GType plugin_service_general_get_type (void);
typedef struct _PluginServiceGeneral PluginServiceGeneral;
typedef struct {
	void (*plugin_func_init) (GnmPluginService *service, ErrorInfo **ret_error);
	void (*plugin_func_cleanup) (GnmPluginService *service, ErrorInfo **ret_error);
} PluginServiceGeneralCallbacks;


#define GNM_PLUGIN_SERVICE_CLIPBOARD_TYPE  (plugin_service_general_get_type ())
#define GNM_PLUGIN_SERVICE_CLIPBOARD(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_PLUGIN_SERVICE_CLIPBOARD_TYPE, PluginServiceClipboard))
#define IS_GNM_PLUGIN_SERVICE_CLIPBOARD(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_PLUGIN_SERVICE_CLIPBOARD_TYPE))

GType plugin_service_general_get_type (void);
typedef struct _PluginServiceClipboard PluginServiceClipboard;
typedef struct {
	guint8     *(*write_content) (GnmPluginService *service, GnmCellRegion *cr, int *size);
	GnmCellRegion *(*read_content)  (GnmPluginService *service, ErrorInfo **ret_error);
} PluginServiceClipboardCallbacks;

#define GNM_PLUGIN_SERVICE_FILE_OPENER_TYPE  (plugin_service_file_opener_get_type ())
#define GNM_PLUGIN_SERVICE_FILE_OPENER(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_PLUGIN_SERVICE_FILE_OPENER_TYPE, PluginServiceFileOpener))
#define IS_GNM_PLUGIN_SERVICE_FILE_OPENER(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_PLUGIN_SERVICE_FILE_OPENER_TYPE))

GType plugin_service_file_opener_get_type (void);
typedef struct _PluginServiceFileOpener PluginServiceFileOpener;
typedef struct {
	/* plugin_func_file_probe may be NULL */
	gboolean (*plugin_func_file_probe) (
	         GnmFileOpener const *fo, GnmPluginService *service,
	         GsfInput *input, FileProbeLevel pl);
	void     (*plugin_func_file_open) (
	         GnmFileOpener const *fo, GnmPluginService *service,
	         IOContext *io_context, GODoc *doc, GsfInput *input);
} PluginServiceFileOpenerCallbacks;


#define GNM_PLUGIN_SERVICE_FILE_SAVER_TYPE  (plugin_service_file_saver_get_type ())
#define GNM_PLUGIN_SERVICE_FILE_SAVER(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_PLUGIN_SERVICE_FILE_SAVER_TYPE, PluginServiceFileSaver))
#define IS_GNM_PLUGIN_SERVICE_FILE_SAVER(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_PLUGIN_SERVICE_FILE_SAVER_TYPE))

GType plugin_service_file_saver_get_type (void);
typedef struct _PluginServiceFileSaver PluginServiceFileSaver;
typedef struct {
	void  (*plugin_func_file_save) (
	      GnmFileSaver const *fs, GnmPluginService *service,
	      IOContext *io_context, WorkbookView const *wbv,
	      GsfOutput *output);
} PluginServiceFileSaverCallbacks;


#define GNM_PLUGIN_SERVICE_FUNCTION_GROUP_TYPE  (plugin_service_function_group_get_type ())
#define GNM_PLUGIN_SERVICE_FUNCTION_GROUP(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_PLUGIN_SERVICE_FUNCTION_GROUP_TYPE, PluginServiceFunctionGroup))
#define IS_GNM_PLUGIN_SERVICE_FUNCTION_GROUP(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_PLUGIN_SERVICE_FUNCTION_GROUP_TYPE))

GType plugin_service_function_group_get_type (void);
typedef struct _PluginServiceFunctionGroup PluginServiceFunctionGroup;
typedef struct {
	gboolean (*func_desc_load) (GnmPluginService *service, char const *name,
				    GnmFuncDescriptor *res);
} PluginServiceFunctionGroupCallbacks;


#define GNM_PLUGIN_SERVICE_PLUGIN_LOADER_TYPE  (plugin_service_plugin_loader_get_type ())
#define GNM_PLUGIN_SERVICE_PLUGIN_LOADER(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_PLUGIN_SERVICE_PLUGIN_LOADER_TYPE, PluginServicePluginLoader))
#define IS_GNM_PLUGIN_SERVICE_PLUGIN_LOADER(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_PLUGIN_SERVICE_PLUGIN_LOADER_TYPE))

GType plugin_service_plugin_loader_get_type (void);
typedef struct _PluginServicePluginLoader PluginServicePluginLoader;
typedef struct {
	GType (*plugin_func_get_loader_type) (
	      GnmPluginService *service, ErrorInfo **ret_error);
} PluginServicePluginLoaderCallbacks;

GType plugin_service_plugin_loader_generate_type (GnmPluginService *service,
                                                  ErrorInfo **ret_error);

#define GNM_PLUGIN_SERVICE_UI_TYPE  (plugin_service_ui_get_type ())
#define GNM_PLUGIN_SERVICE_UI(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_PLUGIN_SERVICE_UI_TYPE, PluginServiceUI))
#define IS_GNM_PLUGIN_SERVICE_UI(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_PLUGIN_SERVICE_UI_TYPE))

GType plugin_service_ui_get_type (void);
typedef struct _PluginServiceUI PluginServiceUI;
typedef struct {
	void (*plugin_func_exec_verb) (
		GnmPluginService *service, WorkbookControl *wbc,
		char const *cname, ErrorInfo **ret_error);
} PluginServiceUICallbacks;

/****************************************************************************/

#define GNM_PLUGIN_SERVICE_GOBJECT_LOADER_TYPE  (plugin_service_gobject_loader_get_type ())
#define GNM_PLUGIN_SERVICE_GOBJECT_LOADER(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_PLUGIN_SERVICE_GOBJECT_LOADER_TYPE, PluginServiceGObjectLoader))
#define IS_GNM_PLUGIN_SERVICE_GOBJECT_LOADER(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_PLUGIN_SERVICE_GOBJECT_LOADER_TYPE))

GType plugin_service_gobject_loader_get_type (void);
typedef struct _PluginServiceGObjectLoader PluginServiceGObjectLoader;

/****************************************************************************/
#define GNM_PLUGIN_SERVICE_SIMPLE_TYPE  (plugin_service_simple_get_type ())
#define GNM_PLUGIN_SERVICE_SIMPLE(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_PLUGIN_SERVICE_SIMPLE_TYPE, PluginServiceSimple))
#define IS_GNM_PLUGIN_SERVICE_SIMPLE(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_PLUGIN_SERVICE_SIMPLE_TYPE))

GType plugin_service_simple_get_type (void);
typedef struct _PluginServiceSimple PluginServiceSimple;

/****************************************************************************/

GnmPluginService  *plugin_service_new (GnmPlugin *plugin, xmlNode *tree, ErrorInfo **ret_error);
char const     *plugin_service_get_id (GnmPluginService *service);
char const     *plugin_service_get_description (GnmPluginService *service);
GnmPlugin      *plugin_service_get_plugin (GnmPluginService *service);
gpointer	plugin_service_get_cbs (GnmPluginService *service);
void		plugin_service_activate (GnmPluginService *service, ErrorInfo **ret_error);
void		plugin_service_deactivate (GnmPluginService *service, ErrorInfo **ret_error);
void		plugin_service_load   (GnmPluginService *service, ErrorInfo **ret_error);
void		plugin_service_unload (GnmPluginService *service, ErrorInfo **ret_error);

typedef GType (*GnmPluginServiceCreate) (void);
void plugin_services_init     (void);
void plugin_services_shutdown (void);
void plugin_service_define    (char const *type_str,
			       GnmPluginServiceCreate ctor);

#endif /* GNUMERIC_PLUGIN_SERVICE_H */

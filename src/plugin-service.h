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
#define GNM_PLUGIN_SERVICE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_PLUGIN_SERVICE_TYPE, PluginService))
#define GNM_IS_PLUGIN_SERVICE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_PLUGIN_SERVICE_TYPE))

GType plugin_service_get_type (void);


#define GNM_PLUGIN_SERVICE_GENERAL_TYPE  (plugin_service_general_get_type ())
#define GNM_PLUGIN_SERVICE_GENERAL(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_PLUGIN_SERVICE_GENERAL_TYPE, PluginServiceGeneral))
#define GNM_IS_PLUGIN_SERVICE_GENERAL(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_PLUGIN_SERVICE_GENERAL_TYPE))

GType plugin_service_general_get_type (void);
typedef struct _PluginServiceGeneral PluginServiceGeneral;
typedef struct {
	void (*plugin_func_init) (PluginService *service, ErrorInfo **ret_error);
	void (*plugin_func_cleanup) (PluginService *service, ErrorInfo **ret_error);
} PluginServiceGeneralCallbacks;


#define GNM_PLUGIN_SERVICE_CLIPBOARD_TYPE  (plugin_service_general_get_type ())
#define GNM_PLUGIN_SERVICE_CLIPBOARD(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_PLUGIN_SERVICE_CLIPBOARD_TYPE, PluginServiceClipboard))
#define GNM_IS_PLUGIN_SERVICE_CLIPBOARD(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_PLUGIN_SERVICE_CLIPBOARD_TYPE))

GType plugin_service_general_get_type (void);
typedef struct _PluginServiceClipboard PluginServiceClipboard;
typedef struct {
	guint8     *(*write_content) (PluginService *service, CellRegion *cr, int *size);
	CellRegion *(*read_content)  (PluginService *service, ErrorInfo **ret_error);
} PluginServiceClipboardCallbacks;

#define GNM_PLUGIN_SERVICE_FILE_OPENER_TYPE  (plugin_service_file_opener_get_type ())
#define GNM_PLUGIN_SERVICE_FILE_OPENER(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_PLUGIN_SERVICE_FILE_OPENER_TYPE, PluginServiceFileOpener))
#define GNM_IS_PLUGIN_SERVICE_FILE_OPENER(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_PLUGIN_SERVICE_FILE_OPENER_TYPE))

GType plugin_service_file_opener_get_type (void);
typedef struct _PluginServiceFileOpener PluginServiceFileOpener;
typedef struct {
	/* plugin_func_file_probe may be NULL */
	gboolean (*plugin_func_file_probe) (
	         GnumFileOpener const *fo, PluginService *service,
	         GsfInput *input, FileProbeLevel pl);
	void     (*plugin_func_file_open) (
	         GnumFileOpener const *fo, PluginService *service,
	         IOContext *io_context, WorkbookView *wbv, GsfInput *input);
} PluginServiceFileOpenerCallbacks;


#define GNM_PLUGIN_SERVICE_FILE_SAVER_TYPE  (plugin_service_file_saver_get_type ())
#define GNM_PLUGIN_SERVICE_FILE_SAVER(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_PLUGIN_SERVICE_FILE_SAVER_TYPE, PluginServiceFileSaver))
#define GNM_IS_PLUGIN_SERVICE_FILE_SAVER(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_PLUGIN_SERVICE_FILE_SAVER_TYPE))

GType plugin_service_file_saver_get_type (void);
typedef struct _PluginServiceFileSaver PluginServiceFileSaver;
typedef struct {
	void  (*plugin_func_file_save) (
	      GnumFileSaver const *fs, PluginService *service,
	      IOContext *io_context, WorkbookView *wbv,
	      const GsfOutput *output);
} PluginServiceFileSaverCallbacks;


#define GNM_PLUGIN_SERVICE_FUNCTION_GROUP_TYPE  (plugin_service_function_group_get_type ())
#define GNM_PLUGIN_SERVICE_FUNCTION_GROUP(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_PLUGIN_SERVICE_FUNCTION_GROUP_TYPE, PluginServiceFunctionGroup))
#define GNM_IS_PLUGIN_SERVICE_FUNCTION_GROUP(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_PLUGIN_SERVICE_FUNCTION_GROUP_TYPE))

GType plugin_service_function_group_get_type (void);
typedef struct _PluginServiceFunctionGroup PluginServiceFunctionGroup;
typedef struct {
	gboolean (*func_desc_load) (PluginService *service, char const *name,
				    GnmFuncDescriptor *res);
} PluginServiceFunctionGroupCallbacks;


#define GNM_PLUGIN_SERVICE_PLUGIN_LOADER_TYPE  (plugin_service_plugin_loader_get_type ())
#define GNM_PLUGIN_SERVICE_PLUGIN_LOADER(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_PLUGIN_SERVICE_PLUGIN_LOADER_TYPE, PluginServicePluginLoader))
#define GNM_IS_PLUGIN_SERVICE_PLUGIN_LOADER(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_PLUGIN_SERVICE_PLUGIN_LOADER_TYPE))

GType plugin_service_plugin_loader_get_type (void);
typedef struct _PluginServicePluginLoader PluginServicePluginLoader;
typedef struct {
	GType (*plugin_func_get_loader_type) (
	      PluginService *service, ErrorInfo **ret_error);
} PluginServicePluginLoaderCallbacks;

GType plugin_service_plugin_loader_generate_type (PluginService *service,
                                                  ErrorInfo **ret_error);

#ifdef WITH_BONOBO
#include <workbook-control-gui.h>

#define GNM_PLUGIN_SERVICE_UI_TYPE  (plugin_service_ui_get_type ())
#define GNM_PLUGIN_SERVICE_UI(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_PLUGIN_SERVICE_UI_TYPE, PluginServiceUI))
#define GNM_IS_PLUGIN_SERVICE_UI(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_PLUGIN_SERVICE_UI_TYPE))

GType plugin_service_ui_get_type (void);
typedef struct _PluginServiceUI PluginServiceUI;
typedef struct {
	void (*plugin_func_exec_verb) (
		PluginService *service, WorkbookControlGUI *wbcg,
		BonoboUIComponent *uic, const char *cname, ErrorInfo **ret_error);
} PluginServiceUICallbacks;

#endif

PluginService *plugin_service_new (xmlNode *tree, ErrorInfo **ret_error);
const char    *plugin_service_get_id (PluginService *service);
const char    *plugin_service_get_description (PluginService *service);
void           plugin_service_set_plugin (PluginService *service, GnmPlugin *plugin);
GnmPlugin     *plugin_service_get_plugin (PluginService *service);
gpointer       plugin_service_get_cbs (PluginService *service);
void           plugin_service_activate (PluginService *service, ErrorInfo **ret_error);
void           plugin_service_deactivate (PluginService *service, ErrorInfo **ret_error);

#endif /* GNUMERIC_PLUGIN_SERVICE_H */

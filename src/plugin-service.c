/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * plugin-service.c: Plugin services - reading XML info, activating, etc.
 *                   (everything independent of plugin loading method)
 *
 * Author: Zbigniew Chyla (cyba@gnome.pl)
 */

#include <gnumeric-config.h>
#include "gnumeric.h"
#include "plugin-service-impl.h"

#include "gutils.h"
#include "workbook.h"
#include "workbook-view.h"
#include "func.h"
#include "io-context.h"
#include "error-info.h"
#include "file.h"
#ifdef WITH_BONOBO
#include <bonobo/bonobo-stream.h>
#endif
#include "file-priv.h"
#include "plugin.h"
#include "xml-io.h"

#include <gsf/gsf-input.h>
#include <gsf/gsf-output.h>
#include <libxml/globals.h>
#include <gsf/gsf-impl-utils.h>
#include <gsf/gsf-utils.h>
#include <glib/gi18n.h>

#include <string.h>

static GHashTable *services = NULL;

static FileFormatLevel
parse_format_level_str (gchar const *format_level_str, FileFormatLevel def)
{
	FileFormatLevel	format_level;

	if (format_level_str == NULL) {
		format_level = def;
	} else if (g_ascii_strcasecmp (format_level_str, "none") == 0) {
		format_level = FILE_FL_NONE;
	} else if (g_ascii_strcasecmp (format_level_str, "write_only") == 0) {
		format_level = FILE_FL_WRITE_ONLY;
	} else if (g_ascii_strcasecmp (format_level_str, "new") == 0) {
		format_level = FILE_FL_NEW;
	} else if (g_ascii_strcasecmp (format_level_str, "manual") == 0) {
		format_level = FILE_FL_MANUAL;
	} else if (g_ascii_strcasecmp (format_level_str, "manual_remember") == 0) {
		format_level = FILE_FL_MANUAL_REMEMBER;
	} else if (g_ascii_strcasecmp (format_level_str, "auto") == 0) {
		format_level = FILE_FL_AUTO;
	} else {
		format_level = def;
	}

	return format_level;
}

static GHashTable *
get_plugin_file_savers_hash (GnmPlugin *plugin)
{
	GHashTable *hash;

	hash = g_object_get_data (G_OBJECT (plugin), "file_savers_hash");
	if (hash == NULL) {
		hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
		g_object_set_data_full (
			G_OBJECT (plugin), "file_savers_hash",
			hash, (GDestroyNotify) g_hash_table_destroy);
	}

	return hash;
}


static void
plugin_service_init (GObject *obj)
{
	GnmPluginService *service = GNM_PLUGIN_SERVICE (obj);

	service->id = NULL;
	service->is_active = FALSE;
	service->is_loaded = FALSE;
	service->plugin = NULL;
	service->cbs_ptr = NULL;
	service->saved_description = NULL;
}

static void
plugin_service_finalize (GObject *obj)
{
	GnmPluginService *service = GNM_PLUGIN_SERVICE (obj);
	GObjectClass *parent_class;

	g_free (service->id);
	service->id = NULL;
	g_free (service->saved_description);
	service->saved_description = NULL;

	parent_class = g_type_class_peek (G_TYPE_OBJECT);
	parent_class->finalize (obj);
}

static void
plugin_service_class_init (GObjectClass *gobject_class)
{
	GnmPluginServiceClass *plugin_service_class = GPS_CLASS (gobject_class);

	gobject_class->finalize = plugin_service_finalize;
	plugin_service_class->read_xml = NULL;
	plugin_service_class->activate = NULL;
	plugin_service_class->deactivate = NULL;
	plugin_service_class->get_description = NULL;
}

GSF_CLASS (GnmPluginService, plugin_service,
	   plugin_service_class_init, plugin_service_init,
           G_TYPE_OBJECT)


/****************************************************************************/

/*
 * PluginServiceGeneral
 */

typedef struct{
	GnmPluginServiceClass plugin_service_class;
} PluginServiceGeneralClass;

struct _PluginServiceGeneral {
	GnmPluginService plugin_service;
	PluginServiceGeneralCallbacks cbs;
};


static void
plugin_service_general_init (GObject *obj)
{
	PluginServiceGeneral *service_general = GNM_PLUGIN_SERVICE_GENERAL (obj);

	GNM_PLUGIN_SERVICE (obj)->cbs_ptr = &service_general->cbs;
	service_general->cbs.plugin_func_init = NULL;
	service_general->cbs.plugin_func_cleanup = NULL;
}

static void
plugin_service_general_activate (GnmPluginService *service, ErrorInfo **ret_error)
{
	PluginServiceGeneral *service_general = GNM_PLUGIN_SERVICE_GENERAL (service);
	ErrorInfo *error = NULL;

	GNM_INIT_RET_ERROR_INFO (ret_error);
	plugin_service_load (service, &error);
	if (error != NULL) {
		*ret_error = error_info_new_str_with_details (
		             _("Error while loading plugin service."),
		             error);
		return;
	}
	g_return_if_fail (service_general->cbs.plugin_func_init != NULL);
	service_general->cbs.plugin_func_init (service, &error);
	if (error != NULL) {
		*ret_error = error_info_new_str_with_details (
		             _("Initializing function inside plugin returned error."),
		             error);
		return;
	}
	service->is_active = TRUE;
}

static void
plugin_service_general_deactivate (GnmPluginService *service, ErrorInfo **ret_error)
{
	PluginServiceGeneral *service_general = GNM_PLUGIN_SERVICE_GENERAL (service);
	ErrorInfo *error = NULL;

	GNM_INIT_RET_ERROR_INFO (ret_error);
	g_return_if_fail (service_general->cbs.plugin_func_cleanup != NULL);
	service_general->cbs.plugin_func_cleanup (service, &error);
	if (error != NULL) {
		*ret_error = error_info_new_str_with_details (
		             _("Cleanup function inside plugin returned error."),
		             error);
		return;
	}
	service->is_active = FALSE;
}

static char *
plugin_service_general_get_description (GnmPluginService *service)
{
	return g_strdup (_("General"));
}

static void
plugin_service_general_class_init (GObjectClass *gobject_class)
{
	GnmPluginServiceClass *plugin_service_class = GPS_CLASS (gobject_class);

	plugin_service_class->activate = plugin_service_general_activate;
	plugin_service_class->deactivate = plugin_service_general_deactivate;
	plugin_service_class->get_description = plugin_service_general_get_description;
}

GSF_CLASS (PluginServiceGeneral, plugin_service_general,
           plugin_service_general_class_init, plugin_service_general_init,
           GNM_PLUGIN_SERVICE_TYPE)

/****************************************************************************/

/*
 * PluginServiceCLipboard
 */

typedef struct{
	GnmPluginServiceClass plugin_service_class;
} PluginServiceClipboardClass;

struct _PluginServiceClipboard {
	GnmPluginService plugin_service;
	PluginServiceClipboardCallbacks cbs;
};

static void
plugin_service_clipboard_init (GObject *obj)
{
	PluginServiceClipboard *service_clipboard = GNM_PLUGIN_SERVICE_CLIPBOARD (obj);

	GNM_PLUGIN_SERVICE (obj)->cbs_ptr = &service_clipboard->cbs;
	service_clipboard->cbs.write_content = NULL;
	service_clipboard->cbs.read_content = NULL;
}

static void
plugin_service_clipboard_activate (GnmPluginService *service, ErrorInfo **ret_error)
{
#if 0
	PluginServiceClipboard *service_clipboard = GNM_PLUGIN_SERVICE_CLIPBOARD (service);
#endif
	ErrorInfo *error = NULL;

	GNM_INIT_RET_ERROR_INFO (ret_error);
	plugin_service_load (service, &error);
	if (error != NULL) {
		*ret_error = error_info_new_str_with_details (
		             _("Error while loading plugin service."),
		             error);
		return;
	}
	service->is_active = TRUE;
}

static void
plugin_service_clipboard_deactivate (GnmPluginService *service, ErrorInfo **ret_error)
{
#if 0
	PluginServiceClipboard *service_clipboard = GNM_PLUGIN_SERVICE_CLIPBOARD (service);
#endif

	GNM_INIT_RET_ERROR_INFO (ret_error);
	service->is_active = FALSE;
}

static char *
plugin_service_clipboard_get_description (GnmPluginService *service)
{
	return g_strdup (_("Clipboard"));
}

static void
plugin_service_clipboard_class_init (GObjectClass *gobject_class)
{
	GnmPluginServiceClass *plugin_service_class = GPS_CLASS (gobject_class);

	plugin_service_class->activate		= plugin_service_clipboard_activate;
	plugin_service_class->deactivate	= plugin_service_clipboard_deactivate;
	plugin_service_class->get_description	= plugin_service_clipboard_get_description;
}

/* disabled for now */
static GSF_CLASS (PluginServiceClipboard, plugin_service_clipboard,
           plugin_service_clipboard_class_init, plugin_service_clipboard_init,
           GNM_PLUGIN_SERVICE_TYPE)

/****************************************************************************/

/*
 * PluginServiceFileOpener
 */

typedef struct _GnmPluginFileOpener GnmPluginFileOpener;
static GnmPluginFileOpener *gnm_plugin_file_opener_new (GnmPluginService *service);

struct _InputFileSaveInfo {
	gchar *saver_id_str;
	FileFormatLevel format_level;
};

typedef struct _InputFileSaveInfo InputFileSaveInfo;


typedef struct{
	GnmPluginServiceClass plugin_service_class;
} PluginServiceFileOpenerClass;

struct _PluginServiceFileOpener {
	GnmPluginService plugin_service;

	gint priority;
	gboolean has_probe;
	gchar *description;
	GSList *suffixes;      /* list of char * */

	GnmFileOpener *opener;
	PluginServiceFileOpenerCallbacks cbs;
};


static void
plugin_service_file_opener_init (GObject *obj)
{
	PluginServiceFileOpener *service_file_opener = GNM_PLUGIN_SERVICE_FILE_OPENER (obj);

	GNM_PLUGIN_SERVICE (obj)->cbs_ptr = &service_file_opener->cbs;
	service_file_opener->description = NULL;
	service_file_opener->suffixes = NULL;
	service_file_opener->opener = NULL;
	service_file_opener->cbs.plugin_func_file_probe = NULL;
	service_file_opener->cbs.plugin_func_file_open = NULL;
}

static void
plugin_service_file_opener_finalize (GObject *obj)
{
	PluginServiceFileOpener *service_file_opener = GNM_PLUGIN_SERVICE_FILE_OPENER (obj);
	GObjectClass *parent_class;

	g_free (service_file_opener->description);
	service_file_opener->description = NULL;
	gnm_slist_free_custom (service_file_opener->suffixes, g_free);
	service_file_opener->suffixes = NULL;
	if (service_file_opener->opener != NULL) {
		g_object_unref (service_file_opener->opener);
		service_file_opener->opener = NULL;
	}

	parent_class = g_type_class_peek (GNM_PLUGIN_SERVICE_TYPE);
	parent_class->finalize (obj);
}

static void
plugin_service_file_opener_read_xml (GnmPluginService *service, xmlNode *tree, ErrorInfo **ret_error)
{
	int priority;
	gboolean has_probe;
	xmlNode *information_node;
	gchar *description;

	GNM_INIT_RET_ERROR_INFO (ret_error);
	if (xml_node_get_int (tree, "priority", &priority))
		priority = CLAMP (priority, 0, 100);
	else
		priority = 50;

	if (!xml_node_get_bool (tree, "probe", &has_probe))
		has_probe = TRUE;

	information_node = e_xml_get_child_by_name (tree, (xmlChar *)"information");
	if (information_node != NULL) {
		xmlNode *node;
		xmlChar *val;

		node = e_xml_get_child_by_name_by_lang (
		       information_node, "description");
		if (node != NULL) {
			val = xmlNodeGetContent (node);
			description = g_strdup ((gchar *)val);
			xmlFree (val);
		} else {
			description = NULL;
		}
	} else {
		description = NULL;
	}
	if (description != NULL) {
		GSList *suffixes = NULL;
		xmlNode *file_patterns_node, *node;
		PluginServiceFileOpener *service_file_opener = GNM_PLUGIN_SERVICE_FILE_OPENER (service);

		file_patterns_node = e_xml_get_child_by_name (tree, (xmlChar *)"suffixes");
		if (file_patterns_node != NULL) {
			char *suffix;
			for (node = file_patterns_node->xmlChildrenNode; node != NULL; node = node->next)
				if (strcmp (node->name, "suffix") == 0 &&
				    (suffix = xmlNodeGetContent (node)) != NULL)
					GNM_SLIST_PREPEND (suffixes, suffix);
		}
		GNM_SLIST_REVERSE (suffixes);

		service_file_opener->priority = priority;
		service_file_opener->has_probe = has_probe;
		service_file_opener->description = description;
		service_file_opener->suffixes = suffixes;
	} else {
		*ret_error = error_info_new_str (_("File opener has no description"));
	}
}

static void
plugin_service_file_opener_activate (GnmPluginService *service, ErrorInfo **ret_error)
{
	PluginServiceFileOpener *service_file_opener = GNM_PLUGIN_SERVICE_FILE_OPENER (service);

	GNM_INIT_RET_ERROR_INFO (ret_error);
	service_file_opener->opener = GNM_FILE_OPENER (gnm_plugin_file_opener_new (service));
	gnm_file_opener_register (service_file_opener->opener,
				  service_file_opener->priority);
	service->is_active = TRUE;
}

static void
plugin_service_file_opener_deactivate (GnmPluginService *service, ErrorInfo **ret_error)
{
	PluginServiceFileOpener *service_file_opener = GNM_PLUGIN_SERVICE_FILE_OPENER (service);

	GNM_INIT_RET_ERROR_INFO (ret_error);
	gnm_file_opener_unregister (service_file_opener->opener);
	service->is_active = FALSE;
}

static char *
plugin_service_file_opener_get_description (GnmPluginService *service)
{
	PluginServiceFileOpener *service_file_opener = GNM_PLUGIN_SERVICE_FILE_OPENER (service);

	return g_strdup_printf (
		_("File opener - %s"), service_file_opener->description);
}

static void
plugin_service_file_opener_class_init (GObjectClass *gobject_class)
{
	GnmPluginServiceClass *plugin_service_class = GPS_CLASS (gobject_class);

	gobject_class->finalize = plugin_service_file_opener_finalize;
	plugin_service_class->read_xml = plugin_service_file_opener_read_xml;
	plugin_service_class->activate = plugin_service_file_opener_activate;
	plugin_service_class->deactivate = plugin_service_file_opener_deactivate;
	plugin_service_class->get_description = plugin_service_file_opener_get_description;
}

GSF_CLASS (PluginServiceFileOpener, plugin_service_file_opener,
           plugin_service_file_opener_class_init, plugin_service_file_opener_init,
           GNM_PLUGIN_SERVICE_TYPE)


/** GnmPluginFileOpener class **/

#define TYPE_GNM_PLUGIN_FILE_OPENER             (gnm_plugin_file_opener_get_type ())
#define GNM_PLUGIN_FILE_OPENER(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_GNM_PLUGIN_FILE_OPENER, GnmPluginFileOpener))
#define GNM_PLUGIN_FILE_OPENER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), TYPE_GNM_PLUGIN_FILE_OPENER, GnmPluginFileOpenerClass))
#define IS_GNM_PLUGIN_FILE_OPENER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_GNM_PLUGIN_FILE_OPENER))

GType gnm_plugin_file_opener_get_type (void);

typedef struct {
	GnmFileOpenerClass parent_class;
} GnmPluginFileOpenerClass;

struct _GnmPluginFileOpener {
	GnmFileOpener parent;

	GnmPluginService *service;
};

static void
gnm_plugin_file_opener_init (GnmPluginFileOpener *fo)
{
	fo->service = NULL;
}

static gboolean
gnm_plugin_file_opener_can_probe (GnmFileOpener const *fo, FileProbeLevel pl)
{
	GnmPluginFileOpener *pfo = GNM_PLUGIN_FILE_OPENER (fo);
	PluginServiceFileOpener *service_file_opener = GNM_PLUGIN_SERVICE_FILE_OPENER (pfo->service);
	if (pl == FILE_PROBE_FILE_NAME)
		return service_file_opener->suffixes != NULL;
	return service_file_opener->has_probe;
}

static gboolean
gnm_plugin_file_opener_probe (GnmFileOpener const *fo, GsfInput *input,
                               FileProbeLevel pl)
{
	GnmPluginFileOpener *pfo = GNM_PLUGIN_FILE_OPENER (fo);
	PluginServiceFileOpener *service_file_opener = GNM_PLUGIN_SERVICE_FILE_OPENER (pfo->service);

	g_return_val_if_fail (GSF_IS_INPUT (input), FALSE);

	if (pl == FILE_PROBE_FILE_NAME && service_file_opener->suffixes != NULL) {
		GSList *ptr;
		gchar const *extension;
		gchar *lowercase_extension;

		if (gsf_input_name (input) == NULL)
			return FALSE;
		extension = gsf_extension_pointer (gsf_input_name (input));
		if (extension == NULL)
			return FALSE;

		lowercase_extension = g_utf8_strdown (extension, -1);
		for (ptr = service_file_opener->suffixes; ptr != NULL ; ptr = ptr->next)
			if (0 == strcmp (lowercase_extension, ptr->data))
				break;
		g_free (lowercase_extension);
		return ptr != NULL;
	}

	if (service_file_opener->has_probe) {
		ErrorInfo *ignored_error = NULL;

		plugin_service_load (pfo->service, &ignored_error);
		if (ignored_error != NULL) {
			error_info_print (ignored_error);
			error_info_free (ignored_error);
			return FALSE;
		} else if (service_file_opener->cbs.plugin_func_file_probe == NULL) {
			return FALSE;
		} else {
			gboolean res = service_file_opener->cbs.plugin_func_file_probe (fo, pfo->service, input, pl);
			gsf_input_seek (input, 0, G_SEEK_SET);
			return res;
		}
	} else {
		return FALSE;
	}
}

static void
gnm_plugin_file_opener_open (GnmFileOpener const *fo, gchar const *unused_enc,
			     IOContext *io_context,
			     GODoc *doc, GsfInput *input)

{
	GnmPluginFileOpener *pfo = GNM_PLUGIN_FILE_OPENER (fo);
	PluginServiceFileOpener *service_file_opener = GNM_PLUGIN_SERVICE_FILE_OPENER (pfo->service);
	ErrorInfo *error = NULL;

	g_return_if_fail (GSF_IS_INPUT (input));

	plugin_service_load (pfo->service, &error);
	if (error != NULL) {
		gnumeric_io_error_info_set (io_context, error);
		gnumeric_io_error_push (io_context, error_info_new_str (
		                        _("Error while reading file.")));
		return;
	}
	g_return_if_fail (service_file_opener->cbs.plugin_func_file_open != NULL);

	service_file_opener->cbs.plugin_func_file_open (fo, pfo->service, io_context, doc, input);
}

static void
gnm_plugin_file_opener_class_init (GnmPluginFileOpenerClass *klass)
{
	GnmFileOpenerClass *gnm_file_opener_klass = GNM_FILE_OPENER_CLASS (klass);

	gnm_file_opener_klass->can_probe = gnm_plugin_file_opener_can_probe;
	gnm_file_opener_klass->Probe = gnm_plugin_file_opener_probe;
	gnm_file_opener_klass->Open = gnm_plugin_file_opener_open;
}

GSF_CLASS (GnmPluginFileOpener, gnm_plugin_file_opener,
	   gnm_plugin_file_opener_class_init, gnm_plugin_file_opener_init,
	   TYPE_GNM_FILE_OPENER)

static GnmPluginFileOpener *
gnm_plugin_file_opener_new (GnmPluginService *service)
{
	PluginServiceFileOpener *service_file_opener = GNM_PLUGIN_SERVICE_FILE_OPENER (service);
	GnmPluginFileOpener *fo;
	gchar *opener_id;

	opener_id = g_strconcat (
		gnm_plugin_get_id (service->plugin), ":", service->id, NULL);
	fo = GNM_PLUGIN_FILE_OPENER (g_object_new (TYPE_GNM_PLUGIN_FILE_OPENER, NULL));
	gnm_file_opener_setup (GNM_FILE_OPENER (fo), opener_id,
	                        service_file_opener->description,
	                        FALSE, NULL, NULL);
	fo->service = service;
	g_free (opener_id);

	return fo;
}

/** -- **/


/*
 * PluginServiceFileSaver
 */

typedef struct _GnmPluginFileSaver GnmPluginFileSaver;
static GnmPluginFileSaver *gnm_plugin_file_saver_new (GnmPluginService *service);


typedef struct{
	GnmPluginServiceClass plugin_service_class;
} PluginServiceFileSaverClass;

struct _PluginServiceFileSaver {
	GnmPluginService plugin_service;

	gchar *file_extension;
	FileFormatLevel format_level;
	gchar *description;
	gint   default_saver_priority;
	FileSaveScope save_scope;
	gboolean overwrite_files;

	GnmFileSaver *saver;
	PluginServiceFileSaverCallbacks cbs;
};


static void
plugin_service_file_saver_init (GObject *obj)
{
	PluginServiceFileSaver *service_file_saver = GNM_PLUGIN_SERVICE_FILE_SAVER (obj);

	GNM_PLUGIN_SERVICE (obj)->cbs_ptr = &service_file_saver->cbs;
	service_file_saver->file_extension = NULL;
	service_file_saver->description = NULL;
	service_file_saver->cbs.plugin_func_file_save = NULL;
	service_file_saver->saver = NULL;
}

static void
plugin_service_file_saver_finalize (GObject *obj)
{
	PluginServiceFileSaver *service_file_saver = GNM_PLUGIN_SERVICE_FILE_SAVER (obj);
	GObjectClass *parent_class;

	g_free (service_file_saver->file_extension);
	service_file_saver->file_extension = NULL;
	g_free (service_file_saver->description);
	service_file_saver->description = NULL;
	if (service_file_saver->saver != NULL) {
		g_object_unref (service_file_saver->saver);
		service_file_saver->saver = NULL;
	}

	parent_class = g_type_class_peek (GNM_PLUGIN_SERVICE_TYPE);
	parent_class->finalize (obj);
}

static void
plugin_service_file_saver_read_xml (GnmPluginService *service, xmlNode *tree, ErrorInfo **ret_error)
{
	gchar *file_extension;
	xmlNode *information_node;
	gchar *description;
	gchar *format_level_str, *save_scope_str;

	GNM_INIT_RET_ERROR_INFO (ret_error);
	file_extension = xmlGetProp (tree, (xmlChar *)"file_extension");
	format_level_str = xmlGetProp (tree, (xmlChar *)"format_level");
	save_scope_str = xmlGetProp (tree, (xmlChar *)"save_scope");
	information_node = e_xml_get_child_by_name (tree, (xmlChar *)"information");
	if (information_node != NULL) {
		xmlNode *node;
		xmlChar *val;

		node = e_xml_get_child_by_name_by_lang (
		       information_node, "description");
		if (node != NULL) {
			val = xmlNodeGetContent (node);
			description = g_strdup ((gchar *)val);
			xmlFree (val);
		} else {
			description = NULL;
		}
	} else {
		description = NULL;
	}
	if (description != NULL) {
		PluginServiceFileSaver *service_file_saver = GNM_PLUGIN_SERVICE_FILE_SAVER (service);

		service_file_saver->file_extension = file_extension;
		service_file_saver->description = description;
		service_file_saver->format_level = parse_format_level_str (format_level_str,
		                                                           FILE_FL_WRITE_ONLY);
		if (!xml_node_get_int (tree, "default_saver_priority", &(service_file_saver->default_saver_priority)))
			service_file_saver->default_saver_priority = -1;

		service_file_saver->save_scope = FILE_SAVE_WORKBOOK;
		if (save_scope_str) {
			if (g_ascii_strcasecmp (save_scope_str, "sheet") == 0)
				service_file_saver->save_scope 
					= FILE_SAVE_SHEET;
			else if (g_ascii_strcasecmp (save_scope_str, 
						     "range") == 0) {
				service_file_saver->save_scope 
					= FILE_SAVE_RANGE;
			}
		}
		if (!xml_node_get_bool (tree, "overwrite_files", &(service_file_saver->overwrite_files)))
			service_file_saver->overwrite_files = TRUE;
	} else {
		*ret_error = error_info_new_str (_("File saver has no description"));
		g_free (file_extension);
	}
	g_free (format_level_str);
	g_free (save_scope_str);
}

static void
plugin_service_file_saver_activate (GnmPluginService *service, ErrorInfo **ret_error)
{
	PluginServiceFileSaver *service_file_saver = GNM_PLUGIN_SERVICE_FILE_SAVER (service);
	GHashTable *file_savers_hash;

	GNM_INIT_RET_ERROR_INFO (ret_error);
	service_file_saver->saver = GNM_FILE_SAVER (gnm_plugin_file_saver_new (service));
	if (service_file_saver->default_saver_priority < 0) {
		gnm_file_saver_register (service_file_saver->saver);
	} else {
		gnm_file_saver_register_as_default (service_file_saver->saver,
						    service_file_saver->default_saver_priority);
	}
	file_savers_hash = get_plugin_file_savers_hash (service->plugin);
	g_assert (g_hash_table_lookup (file_savers_hash, service->id) == NULL);
	g_hash_table_insert (file_savers_hash, g_strdup (service->id), service_file_saver->saver);
	service->is_active = TRUE;
}

static void
plugin_service_file_saver_deactivate (GnmPluginService *service, ErrorInfo **ret_error)
{
	PluginServiceFileSaver *service_file_saver = GNM_PLUGIN_SERVICE_FILE_SAVER (service);
	GHashTable *file_savers_hash;

	GNM_INIT_RET_ERROR_INFO (ret_error);
	file_savers_hash = get_plugin_file_savers_hash (service->plugin);
	g_hash_table_remove (file_savers_hash, service->id);
	gnm_file_saver_unregister (service_file_saver->saver);
	service->is_active = FALSE;
}

static char *
plugin_service_file_saver_get_description (GnmPluginService *service)
{
	PluginServiceFileSaver *service_file_saver = GNM_PLUGIN_SERVICE_FILE_SAVER (service);

	return g_strdup_printf (
		_("File saver - %s"), service_file_saver->description);
}

static void
plugin_service_file_saver_class_init (GObjectClass *gobject_class)
{
	GnmPluginServiceClass *plugin_service_class = GPS_CLASS (gobject_class);

	gobject_class->finalize = plugin_service_file_saver_finalize;
	plugin_service_class->read_xml = plugin_service_file_saver_read_xml;
	plugin_service_class->activate = plugin_service_file_saver_activate;
	plugin_service_class->deactivate = plugin_service_file_saver_deactivate;
	plugin_service_class->get_description = plugin_service_file_saver_get_description;
}

GSF_CLASS (PluginServiceFileSaver, plugin_service_file_saver,
           plugin_service_file_saver_class_init, plugin_service_file_saver_init,
           GNM_PLUGIN_SERVICE_TYPE)


/** GnmPluginFileSaver class **/

#define TYPE_GNM_PLUGIN_FILE_SAVER             (gnm_plugin_file_saver_get_type ())
#define GNM_PLUGIN_FILE_SAVER(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_GNM_PLUGIN_FILE_SAVER, GnmPluginFileSaver))
#define GNM_PLUGIN_FILE_SAVER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), TYPE_GNM_PLUGIN_FILE_SAVER, GnmPluginFileSaverClass))
#define IS_GNM_PLUGIN_FILE_SAVER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_GNM_PLUGIN_FILE_SAVER))

GType gnm_plugin_file_saver_get_type (void);

typedef struct {
	GnmFileSaverClass parent_class;
} GnmPluginFileSaverClass;

struct _GnmPluginFileSaver {
	GnmFileSaver parent;

	GnmPluginService *service;
};

static void
gnm_plugin_file_saver_init (GnmPluginFileSaver *fs)
{
	fs->service = NULL;
}

static void
gnm_plugin_file_saver_save (GnmFileSaver const *fs, IOContext *io_context,
                             WorkbookView const *wbv, GsfOutput *output)
{
	GnmPluginFileSaver *pfs = GNM_PLUGIN_FILE_SAVER (fs);
	PluginServiceFileSaver *service_file_saver = GNM_PLUGIN_SERVICE_FILE_SAVER (pfs->service);
	ErrorInfo *error = NULL;

	g_return_if_fail (GSF_IS_OUTPUT (output));

	plugin_service_load (pfs->service, &error);
	if (error == NULL) {
		g_return_if_fail (service_file_saver->cbs.plugin_func_file_save != NULL);
		service_file_saver->cbs.plugin_func_file_save (fs, pfs->service, io_context, wbv, output);
	} else {
		gnumeric_io_error_info_set (io_context, error);
		gnumeric_io_error_push (io_context, error_info_new_str (
		                        _("Error while saving file.")));
	}
}

static void
gnm_plugin_file_saver_class_init (GnmPluginFileSaverClass *klass)
{
	GnmFileSaverClass *gnm_file_saver_klass = GNM_FILE_SAVER_CLASS (klass);

	gnm_file_saver_klass->save = gnm_plugin_file_saver_save;
}

GSF_CLASS (GnmPluginFileSaver, gnm_plugin_file_saver,
	   gnm_plugin_file_saver_class_init, gnm_plugin_file_saver_init,
	   TYPE_GNM_FILE_SAVER)

static GnmPluginFileSaver *
gnm_plugin_file_saver_new (GnmPluginService *service)
{
	GnmPluginFileSaver *fs;
	PluginServiceFileSaver *service_file_saver = GNM_PLUGIN_SERVICE_FILE_SAVER (service);
	gchar *saver_id;

	saver_id = g_strconcat (
		gnm_plugin_get_id (service->plugin), ":", service->id, NULL);
	fs = GNM_PLUGIN_FILE_SAVER (g_object_new (TYPE_GNM_PLUGIN_FILE_SAVER, NULL));
	gnm_file_saver_setup (GNM_FILE_SAVER (fs), saver_id,
	                       service_file_saver->file_extension,
	                       service_file_saver->description,
	                       service_file_saver->format_level,
	                       NULL);
	gnm_file_saver_set_save_scope (GNM_FILE_SAVER (fs),
	                                service_file_saver->save_scope);
	gnm_file_saver_set_overwrite_files (GNM_FILE_SAVER (fs),
	                                     service_file_saver->overwrite_files);
	fs->service = service;
	g_free (saver_id);

	return fs;
}

/** -- **/

/*
 * PluginServiceFunctionGroup
 */

typedef struct{
	GnmPluginServiceClass plugin_service_class;
} PluginServiceFunctionGroupClass;

struct _PluginServiceFunctionGroup {
	GnmPluginService plugin_service;

	gchar *category_name, *translated_category_name;
	GSList *function_name_list;

	GnmFuncGroup	*func_group;
	PluginServiceFunctionGroupCallbacks cbs;
};


static void
plugin_service_function_group_init (GObject *obj)
{
	PluginServiceFunctionGroup *service_function_group = GNM_PLUGIN_SERVICE_FUNCTION_GROUP (obj);

	GNM_PLUGIN_SERVICE (obj)->cbs_ptr = &service_function_group->cbs;
	service_function_group->category_name = NULL;
	service_function_group->translated_category_name = NULL;
	service_function_group->function_name_list = NULL;
	service_function_group->func_group = NULL;
}

static void
plugin_service_function_group_finalize (GObject *obj)
{
	PluginServiceFunctionGroup *service_function_group = GNM_PLUGIN_SERVICE_FUNCTION_GROUP (obj);
	GObjectClass *parent_class;

	g_free (service_function_group->category_name);
	service_function_group->category_name = NULL;
	g_free (service_function_group->translated_category_name);
	service_function_group->translated_category_name = NULL;
	gnm_slist_free_custom (service_function_group->function_name_list, g_free);
	service_function_group->function_name_list = NULL;

	parent_class = g_type_class_peek (GNM_PLUGIN_SERVICE_TYPE);
	parent_class->finalize (obj);
}

static void
plugin_service_function_group_read_xml (GnmPluginService *service, xmlNode *tree, ErrorInfo **ret_error)
{
	xmlNode *category_node, *translated_category_node, *functions_node;
	gchar *category_name, *translated_category_name;
	GSList *function_name_list = NULL;

	GNM_INIT_RET_ERROR_INFO (ret_error);
	category_node = e_xml_get_child_by_name_no_lang (tree, "category");
	if (category_node != NULL) {
		xmlChar *val;

		val = xmlNodeGetContent (category_node);
		category_name = g_strdup ((gchar *)val);
		xmlFree (val);
	} else {
		category_name = NULL;
	}
	translated_category_node = e_xml_get_child_by_name_by_lang (tree, "category");
	if (translated_category_node != NULL) {
		gchar *lang;

		lang = xmlGetProp (translated_category_node, (xmlChar *)"xml:lang");
		if (lang != NULL) {
			xmlChar *val;

			val = xmlNodeGetContent (translated_category_node);
			translated_category_name = g_strdup ((gchar *)val);
			xmlFree (val);
			g_free (lang);
		} else {
			translated_category_name = NULL;
		}
	} else {
		translated_category_name = NULL;
	}
	functions_node = e_xml_get_child_by_name (tree, (xmlChar *)"functions");
	if (functions_node != NULL) {
		xmlNode *node;

		for (node = functions_node->xmlChildrenNode; node != NULL; node = node->next) {
			gchar *func_name;

			if (strcmp (node->name, "function") != 0 ||
			    (func_name = xmlGetProp (node, (xmlChar *)"name")) == NULL) {
				continue;
			}
			GNM_SLIST_PREPEND (function_name_list, func_name);
		}
		GNM_SLIST_REVERSE (function_name_list);
	}
	if (category_name != NULL && function_name_list != NULL) {
		PluginServiceFunctionGroup *service_function_group = GNM_PLUGIN_SERVICE_FUNCTION_GROUP (service);

		service_function_group->category_name = category_name;
		service_function_group->translated_category_name = translated_category_name;
		service_function_group->function_name_list = function_name_list;
	} else {
		GSList *error_list = NULL;

		if (category_name == NULL) {
			GNM_SLIST_PREPEND (error_list, error_info_new_str (
				_("Missing function category name.")));
		}
		if (function_name_list == NULL) {
			GNM_SLIST_PREPEND (error_list, error_info_new_str (
				_("Function group is empty.")));
		}
		GNM_SLIST_REVERSE (error_list);
		*ret_error = error_info_new_from_error_list (error_list);

		g_free (category_name);
		g_free (translated_category_name);
		gnm_slist_free_custom (function_name_list, g_free);
	}
}

static gboolean
plugin_service_function_group_func_desc_load (GnmFunc const *fn_def,
					      GnmFuncDescriptor *res)
{
	GnmPluginService *service;
	PluginServiceFunctionGroup *service_function_group;
	ErrorInfo *error = NULL;

	g_return_val_if_fail (fn_def != NULL, FALSE);

	service = gnm_func_get_user_data (fn_def);
	service_function_group = GNM_PLUGIN_SERVICE_FUNCTION_GROUP (service);
	plugin_service_load (service, &error);
	if (error != NULL) {
		error_info_print (error);
		error_info_free (error);
		return FALSE;
	}
	return service_function_group->cbs.func_desc_load (service,
		gnm_func_get_name (fn_def), res);
}

static void
plugin_service_function_group_func_ref_notify  (GnmFunc *fn_def, int refcount)
{
	GnmPluginService *service;

	service = gnm_func_get_user_data (fn_def);
	g_return_if_fail (IS_GNM_PLUGIN_SERVICE_FUNCTION_GROUP (service));
	if (refcount == 0) {
		gnm_plugin_use_unref (service->plugin);
	} else {
		gnm_plugin_use_ref (service->plugin);
	}
}

static void
plugin_service_function_group_activate (GnmPluginService *service, ErrorInfo **ret_error)
{
	PluginServiceFunctionGroup *service_function_group = GNM_PLUGIN_SERVICE_FUNCTION_GROUP (service);

	GNM_INIT_RET_ERROR_INFO (ret_error);
	service_function_group->func_group = gnm_func_group_fetch_with_translation (
		service_function_group->category_name,
		service_function_group->translated_category_name);
	GNM_SLIST_FOREACH (service_function_group->function_name_list, char, fname,
		GnmFunc *fn_def;

		fn_def = gnm_func_add_stub (
			service_function_group->func_group, fname,
			plugin_service_function_group_func_desc_load,
			plugin_service_function_group_func_ref_notify);
		gnm_func_set_user_data (fn_def, service);
	);
	service->is_active = TRUE;
}

static void
plugin_service_function_group_deactivate (GnmPluginService *service, ErrorInfo **ret_error)
{
	PluginServiceFunctionGroup *service_function_group = GNM_PLUGIN_SERVICE_FUNCTION_GROUP (service);

	GNM_INIT_RET_ERROR_INFO (ret_error);
	GNM_SLIST_FOREACH (service_function_group->function_name_list, char, fname,
		gnm_func_free (gnm_func_lookup (fname, NULL));
	);
	service->is_active = FALSE;
}

static char *
plugin_service_function_group_get_description (GnmPluginService *service)
{
	PluginServiceFunctionGroup *service_function_group = GNM_PLUGIN_SERVICE_FUNCTION_GROUP (service);
	int n_functions;
	char const *category_name;

	n_functions = g_slist_length (service_function_group->function_name_list);
	category_name = service_function_group->translated_category_name != NULL
		? service_function_group->translated_category_name
		: service_function_group->category_name;

	return g_strdup_printf (
		ngettext (
			N_("%d function in category \"%s\""),
			N_("Group of %d functions in category \"%s\""),
			n_functions),
		n_functions, category_name);
}

static void
plugin_service_function_group_class_init (GObjectClass *gobject_class)
{
	GnmPluginServiceClass *plugin_service_class = GPS_CLASS (gobject_class);

	gobject_class->finalize = plugin_service_function_group_finalize;
	plugin_service_class->read_xml = plugin_service_function_group_read_xml;
	plugin_service_class->activate = plugin_service_function_group_activate;
	plugin_service_class->deactivate = plugin_service_function_group_deactivate;
	plugin_service_class->get_description = plugin_service_function_group_get_description;
}

GSF_CLASS (PluginServiceFunctionGroup, plugin_service_function_group,
           plugin_service_function_group_class_init, plugin_service_function_group_init,
           GNM_PLUGIN_SERVICE_TYPE)


/*
 * PluginServicePluginLoader
 */

typedef struct{
	GnmPluginServiceClass plugin_service_class;
} PluginServicePluginLoaderClass;

struct _PluginServicePluginLoader {
	GnmPluginService plugin_service;
	PluginServicePluginLoaderCallbacks cbs;
};


static void
plugin_service_plugin_loader_init (GObject *obj)
{
	PluginServicePluginLoader *service_plugin_loader = GNM_PLUGIN_SERVICE_PLUGIN_LOADER (obj);

	GNM_PLUGIN_SERVICE (obj)->cbs_ptr = &service_plugin_loader->cbs;
}

GType
plugin_service_plugin_loader_generate_type (GnmPluginService *service, ErrorInfo **ret_error)
{
	PluginServicePluginLoader *service_plugin_loader = GNM_PLUGIN_SERVICE_PLUGIN_LOADER (service);
	ErrorInfo *error = NULL;
	GType loader_type;

	GNM_INIT_RET_ERROR_INFO (ret_error);
	plugin_service_load (service, &error);
	if (error == NULL) {
		loader_type = service_plugin_loader->cbs.plugin_func_get_loader_type (
			service, &error);
		if (error == NULL)
			return loader_type;
		*ret_error = error;
	} else {
		*ret_error = error_info_new_str_with_details (
		             _("Error while loading plugin service."),
		             error);
	}
	return G_TYPE_NONE;
}

static void
plugin_service_plugin_loader_activate (GnmPluginService *service, ErrorInfo **ret_error)
{
	gchar *full_id;

	GNM_INIT_RET_ERROR_INFO (ret_error);
	full_id = g_strconcat (
		gnm_plugin_get_id (service->plugin), ":", service->id, NULL);
	plugins_register_loader (full_id, service);
	g_free (full_id);
	service->is_active = TRUE;
}

static void
plugin_service_plugin_loader_deactivate (GnmPluginService *service, ErrorInfo **ret_error)
{
	gchar *full_id;

	GNM_INIT_RET_ERROR_INFO (ret_error);
	full_id = g_strconcat (
		gnm_plugin_get_id (service->plugin), ":", service->id, NULL);
	plugins_register_loader (full_id, service);
	g_free (full_id);
	service->is_active = FALSE;
}

static char *
plugin_service_plugin_loader_get_description (GnmPluginService *service)
{
	return g_strdup (_("Plugin loader"));
}

static void
plugin_service_plugin_loader_class_init (GObjectClass *gobject_class)
{
	GnmPluginServiceClass *plugin_service_class = GPS_CLASS (gobject_class);

	plugin_service_class->activate = plugin_service_plugin_loader_activate;
	plugin_service_class->deactivate = plugin_service_plugin_loader_deactivate;
	plugin_service_class->get_description = plugin_service_plugin_loader_get_description;
}

GSF_CLASS (PluginServicePluginLoader, plugin_service_plugin_loader,
           plugin_service_plugin_loader_class_init, plugin_service_plugin_loader_init,
           GNM_PLUGIN_SERVICE_TYPE)


#ifdef WITH_BONOBO
/*
 * PluginServiceUI
 */
#include <workbook-control-gui.h>

typedef struct{
	GnmPluginServiceClass plugin_service_class;
} PluginServiceUIClass;

struct _PluginServiceUI {
	GnmPluginService plugin_service;

	char *file_name;
	GSList *verbs;

	CustomXmlUI *ui;
	PluginServiceUICallbacks cbs;
};

static void
plugin_service_ui_init (GObject *obj)
{
	PluginServiceUI *service_ui = GNM_PLUGIN_SERVICE_UI (obj);

	GNM_PLUGIN_SERVICE (obj)->cbs_ptr = &service_ui->cbs;
	service_ui->file_name = NULL;
	service_ui->verbs = NULL;
	service_ui->ui = NULL;
	service_ui->cbs.plugin_func_exec_verb = NULL;
}

static void
plugin_service_ui_finalize (GObject *obj)
{
	PluginServiceUI *service_ui = GNM_PLUGIN_SERVICE_UI (obj);
	GObjectClass *parent_class;

	g_free (service_ui->file_name);
	service_ui->file_name = NULL;
	gnm_slist_free_custom (service_ui->verbs, g_free);
	service_ui->verbs = NULL;

	parent_class = g_type_class_peek (GNM_PLUGIN_SERVICE_TYPE);
	parent_class->finalize (obj);
}

static void
plugin_service_ui_read_xml (GnmPluginService *service, xmlNode *tree, ErrorInfo **ret_error)
{
	PluginServiceUI *service_ui = GNM_PLUGIN_SERVICE_UI (service);
	char *file_name;
	xmlNode *verbs_node;
	GSList *verbs = NULL;

	GNM_INIT_RET_ERROR_INFO (ret_error);
	file_name = xmlGetProp (tree, "file");
	if (file_name == NULL) {
		*ret_error = error_info_new_str (
		             _("Missing file name."));
		return;
	}
	verbs_node = e_xml_get_child_by_name (tree, "verbs");
	if (verbs_node != NULL) {
		xmlNode *node;

		for (node = verbs_node->xmlChildrenNode; node != NULL; node = node->next) {
			char *name;

			if (strcmp (node->name, "verb") == 0 &&
			    (name = xmlGetProp (node, "name")) != NULL) {
				GNM_SLIST_PREPEND (verbs, name);
			}
		}
	}
	GNM_SLIST_REVERSE (verbs);

	service_ui->file_name = file_name;
	service_ui->verbs = verbs;
}

static void
ui_verb_fn (BonoboUIComponent *uic, gpointer user_data, gchar const *cname)
{
	GnmPluginService *service = GNM_PLUGIN_SERVICE (user_data);
	ErrorInfo *load_error = NULL;

	plugin_service_load (service, &load_error);
	if (load_error == NULL) {
		PluginServiceUI *service_ui = GNM_PLUGIN_SERVICE_UI (service);
		WorkbookControlGUI *wbcg;
		ErrorInfo *ignored_error = NULL;

		g_return_if_fail (service_ui->cbs.plugin_func_exec_verb != NULL);
		wbcg = g_object_get_data (
			G_OBJECT (uic), "gnumeric-workbook-control-gui");
		g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));
		service_ui->cbs.plugin_func_exec_verb (
			service, wbcg, uic, cname, &ignored_error);
		if (ignored_error != NULL) {
			error_info_print (ignored_error);
			error_info_free (ignored_error);
		}
	} else {
		error_info_print (load_error);
		error_info_free (load_error);
	}
}

static void
plugin_service_ui_activate (GnmPluginService *service, ErrorInfo **ret_error)
{
	PluginServiceUI *service_ui = GNM_PLUGIN_SERVICE_UI (service);
	char *full_file_name;
	BonoboUINode *uinode;
	char *xml_ui;
	char const *textdomain;

	GNM_INIT_RET_ERROR_INFO (ret_error);
	full_file_name = g_build_filename (
		go_plugin_get_dir (service->plugin),
		service_ui->file_name, NULL);
	uinode = bonobo_ui_node_from_file (full_file_name);
	if (uinode == NULL) {
		*ret_error = error_info_new_printf (
		             _("Cannot read UI description from XML file %s."),
		             full_file_name);
		g_free (full_file_name);
		return;
	}
	g_free (full_file_name);

	xml_ui = bonobo_ui_node_to_string (uinode, TRUE);
	bonobo_ui_node_free (uinode);

	textdomain = gnm_plugin_get_textdomain (service->plugin);
	service_ui->ui = register_xml_ui (
		xml_ui, textdomain, service_ui->verbs,
		G_CALLBACK (ui_verb_fn), service);
	service->is_active = TRUE;
	g_free (xml_ui);
}

static void
plugin_service_ui_deactivate (GnmPluginService *service, ErrorInfo **ret_error)
{
	PluginServiceUI *service_ui = GNM_PLUGIN_SERVICE_UI (service);

	GNM_INIT_RET_ERROR_INFO (ret_error);
	unregister_xml_ui (service_ui->ui);
	service->is_active = FALSE;
}

static char *
plugin_service_ui_get_description (GnmPluginService *service)
{
	PluginServiceUI *service_ui = GNM_PLUGIN_SERVICE_UI (service);
	int n_verbs;

	n_verbs = g_slist_length (service_ui->verbs);
	return g_strdup_printf (
		ngettext (
			N_("User interface with %d action"),
			N_("User interface with %d actions"),
			n_verbs),
		n_verbs);
}

static void
plugin_service_ui_class_init (GObjectClass *gobject_class)
{
	GnmPluginServiceClass *plugin_service_class = GPS_CLASS (gobject_class);

	gobject_class->finalize = plugin_service_ui_finalize;
	plugin_service_class->read_xml = plugin_service_ui_read_xml;
	plugin_service_class->activate = plugin_service_ui_activate;
	plugin_service_class->deactivate = plugin_service_ui_deactivate;
	plugin_service_class->get_description = plugin_service_ui_get_description;
}

GSF_CLASS (PluginServiceUI, plugin_service_ui,
           plugin_service_ui_class_init, plugin_service_ui_init,
           GNM_PLUGIN_SERVICE_TYPE)

#endif

/**************************************************************************
 * PluginServiceGObjectLoader
 */

static char *
plugin_service_gobject_loader_get_description (GnmPluginService *service)
{
	return g_strdup (_("GObject loader"));
}

static void
plugin_service_gobject_loader_read_xml (GnmPluginService *service,
					G_GNUC_UNUSED xmlNode *tree,
					G_GNUC_UNUSED ErrorInfo **ret_error)
{
	PluginServiceGObjectLoaderClass *gobj_loader_class = GPS_GOBJECT_LOADER_GET_CLASS (service);
	g_return_if_fail (gobj_loader_class->pending != NULL);
	g_hash_table_replace (gobj_loader_class->pending, service->id, service);
}

static void
plugin_service_gobject_loader_class_init (PluginServiceGObjectLoaderClass *gobj_loader_class)
{
	GnmPluginServiceClass *psc = GPS_CLASS (gobj_loader_class);

	psc->get_description	= plugin_service_gobject_loader_get_description;
	psc->read_xml		= plugin_service_gobject_loader_read_xml;
	gobj_loader_class->pending = NULL;
}

GSF_CLASS (PluginServiceGObjectLoader, plugin_service_gobject_loader,
           plugin_service_gobject_loader_class_init, NULL,
           GNM_PLUGIN_SERVICE_SIMPLE_TYPE)

/**************************************************************************
 * PluginServiceSimple
 */

static void
plugin_service_simple_activate (GnmPluginService *service, ErrorInfo **ret_error)
{
	service->is_active = TRUE;
}

static void
plugin_service_simple_deactivate (GnmPluginService *service, ErrorInfo **ret_error)
{
	service->is_active = FALSE;
}

static void
plugin_service_simple_class_init (GObjectClass *gobject_class)
{
	GnmPluginServiceClass *psc = GPS_CLASS (gobject_class);

	psc->activate		= plugin_service_simple_activate;
	psc->deactivate		= plugin_service_simple_deactivate;
}

GSF_CLASS (PluginServiceSimple, plugin_service_simple,
           plugin_service_simple_class_init,
	   NULL,
           GNM_PLUGIN_SERVICE_TYPE)

/* ---------------------------------------------------------------------- */

void
plugin_service_load (GnmPluginService *service, ErrorInfo **ret_error)
{
	g_return_if_fail (IS_GNM_PLUGIN_SERVICE (service));

	GNM_INIT_RET_ERROR_INFO (ret_error);

	if (service->is_loaded)
		return;
	gnm_plugin_load_service (service->plugin, service, ret_error);
	if (*ret_error == NULL)
		service->is_loaded = TRUE;
}

void
plugin_service_unload (GnmPluginService *service, ErrorInfo **ret_error)
{
	ErrorInfo *error = NULL;

	g_return_if_fail (IS_GNM_PLUGIN_SERVICE (service));

	GNM_INIT_RET_ERROR_INFO (ret_error);
	if (!service->is_loaded) {
		return;
	}
	gnm_plugin_unload_service (service->plugin, service, &error);
	if (error == NULL) {
		service->is_loaded = FALSE;
	} else {
		*ret_error = error;
	}
}

GnmPluginService *
plugin_service_new (GnmPlugin *plugin, xmlNode *tree, ErrorInfo **ret_error)
{
	GnmPluginService *service = NULL;
	char *type_str;
	ErrorInfo *service_error = NULL;
	GnmPluginServiceCreate ctor;

	g_return_val_if_fail (IS_GNM_PLUGIN (plugin), NULL);
	g_return_val_if_fail (tree != NULL, NULL);
	g_return_val_if_fail (strcmp (tree->name, "service") == 0, NULL);

	GNM_INIT_RET_ERROR_INFO (ret_error);
	type_str = xmlGetProp (tree, (xmlChar *) "type");
	if (type_str == NULL) {
		*ret_error = error_info_new_str (_("No \"type\" attribute on \"service\" element."));
		return NULL;
	}

	ctor = g_hash_table_lookup (services, type_str);
	if (ctor == NULL) {
		*ret_error = error_info_new_printf (_("Unknown service type: %s."), type_str);
		g_free (type_str);
		return NULL;
	}
	g_free (type_str);

	service = g_object_new (ctor(), NULL);
	service->plugin = plugin;
	service->id = xmlGetProp (tree, (xmlChar *) "id");
	if (service->id == NULL)
		service->id = g_strdup ("default");

	if (GPS_GET_CLASS (service)->read_xml != NULL) {
		GPS_GET_CLASS (service)->read_xml (service, tree, &service_error);
		if (service_error != NULL) {
			*ret_error = error_info_new_str_with_details (
				_("Error reading service information."), service_error);
			g_object_unref (service);
			service = NULL;
		}
	}

	return service;
}

char const *
plugin_service_get_id (GnmPluginService *service)
{
	g_return_val_if_fail (IS_GNM_PLUGIN_SERVICE (service), NULL);

	return service->id;
}

char const *
plugin_service_get_description (GnmPluginService *service)
{
	g_return_val_if_fail (IS_GNM_PLUGIN_SERVICE (service), NULL);

	if (service->saved_description == NULL) {
		service->saved_description = GPS_GET_CLASS (service)->get_description (service);
	}

	return service->saved_description;
}

GnmPlugin *
plugin_service_get_plugin (GnmPluginService *service)
{
	g_return_val_if_fail (IS_GNM_PLUGIN_SERVICE (service), NULL);

	return service->plugin;
}

gpointer
plugin_service_get_cbs (GnmPluginService *service)
{
	g_return_val_if_fail (IS_GNM_PLUGIN_SERVICE (service), NULL);
	g_return_val_if_fail (service->cbs_ptr != NULL, NULL);

	return service->cbs_ptr;
}

void
plugin_service_activate (GnmPluginService *service, ErrorInfo **ret_error)
{
	g_return_if_fail (IS_GNM_PLUGIN_SERVICE (service));

	GNM_INIT_RET_ERROR_INFO (ret_error);
	if (service->is_active) {
		return;
	}
#ifdef PLUGIN_ALWAYS_LOAD
	{
		ErrorInfo *load_error = NULL;

		plugin_service_load (service, &load_error);
		if (load_error != NULL) {
			*ret_error = error_info_new_str_with_details (
				_("We must load service before activating it (PLUGIN_ALWAYS_LOAD is set) "
				  "but loading failed."), load_error);
			return;
		}
	}
#endif
	GPS_GET_CLASS (service)->activate (service, ret_error);
}

void
plugin_service_deactivate (GnmPluginService *service, ErrorInfo **ret_error)
{
	g_return_if_fail (IS_GNM_PLUGIN_SERVICE (service));

	GNM_INIT_RET_ERROR_INFO (ret_error);
	if (!service->is_active) {
		return;
	}
	GPS_GET_CLASS (service)->deactivate (service, ret_error);
	if (*ret_error == NULL) {
		ErrorInfo *ignored_error = NULL;

		service->is_active = FALSE;
		/* FIXME */
		plugin_service_unload (service, &ignored_error);
		error_info_free (ignored_error);
	}
}

/*****************************************************************************/

void
plugin_services_init (void)
{
	static struct {
		char const *type_str;
		GnmPluginServiceCreate ctor;
	} const builtin_services[] = {
		{ "general",		plugin_service_general_get_type},
		{ "clipboard",		plugin_service_clipboard_get_type},
		{ "file_opener",	plugin_service_file_opener_get_type},
		{ "file_saver",		plugin_service_file_saver_get_type},
		{ "function_group",	plugin_service_function_group_get_type},
		{ "plugin_loader",	plugin_service_plugin_loader_get_type}
#ifdef WITH_BONOBO
		,{"ui",			plugin_service_ui_get_type}
#endif
/* base classes, not really for direct external use,
 * put here for expositional purposes
 */
#if 0
		{ "gobject_loader",	plugin_service_gobject_loader_get_type}
		{ "simple",		plugin_service_simple_get_type}
#endif
	};
	unsigned i;

	g_return_if_fail (services == NULL);

	services = g_hash_table_new (g_str_hash, g_str_equal);
	for (i = 0; i < G_N_ELEMENTS (builtin_services); i++)
		plugin_service_define (builtin_services[i].type_str,
				       builtin_services[i].ctor);
}

void
plugin_services_shutdown (void)
{
	g_return_if_fail (services != NULL);
	g_hash_table_destroy (services);
	services = NULL;
}

/**
 * Allow the definition of new service types
 **/
void
plugin_service_define (char const *type_str, GnmPluginServiceCreate ctor)
{
	g_return_if_fail (services != NULL);

	g_return_if_fail (NULL == g_hash_table_lookup (services, type_str));

	g_hash_table_insert (services, (gpointer)type_str, ctor);
}

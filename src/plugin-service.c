/*
 * plugin-service.c: Plugin services - reading XML info, activating, etc.
 *                   (everything independent of plugin loading method)
 *
 * Author: Zbigniew Chyla (cyba@gnome.pl)
 */

#include <config.h>
#include <fnmatch.h>
#include <glib.h>
#include <gnome-xml/tree.h>
#include <libgnome/libgnome.h>
#include <gal/util/e-xml-utils.h>
#include <gal/util/e-util.h>
#include "portability.h"
#include "gnumeric.h"
#include "workbook.h"
#include "workbook-view.h"
#include "func.h"
#include "io-context.h"
#include "error-info.h"
#include "file.h"
#include "file-priv.h"
#include "plugin.h"
#include "plugin-service.h"

typedef enum {FILE_PATTERN_SHELL, FILE_PATTERN_REGEXP, FILE_PATTERN_LAST} InputFilePatternType;

struct _InputFilePattern {
	InputFilePatternType pattern_type;
	gboolean case_sensitive;
	gchar *value;
};

struct _InputFileSaveInfo {
	gchar *saver_id_str;
	FileFormatLevel format_level;
};

struct _PluginServicesData {
	GHashTable *file_savers_hash;
};

static void plugin_service_init (PluginService *service, PluginServiceType service_type);
static void plugin_service_load (PluginService *service, ErrorInfo **ret_error);
static void plugin_service_unload (PluginService *service, ErrorInfo **ret_error);

static FileFormatLevel
parse_format_level_str (const gchar *format_level_str, FileFormatLevel def)
{
	FileFormatLevel	format_level;

	if (format_level_str == NULL) {
		format_level = def;
	} else if (g_strcasecmp (format_level_str, "none") == 0) {
		format_level = FILE_FL_NONE;
	} else if (g_strcasecmp (format_level_str, "write_only") == 0) {
		format_level = FILE_FL_WRITE_ONLY;
	} else if (g_strcasecmp (format_level_str, "new") == 0) {
		format_level = FILE_FL_NEW;
	} else if (g_strcasecmp (format_level_str, "manual") == 0) {
		format_level = FILE_FL_MANUAL;
	} else if (g_strcasecmp (format_level_str, "manual_remember") == 0) {
		format_level = FILE_FL_MANUAL_REMEMBER;
	} else if (g_strcasecmp (format_level_str, "auto") == 0) {
		format_level = FILE_FL_AUTO;
	} else {
		format_level = def;
	}

	return format_level;
}

/*
 * General
 */

static PluginService *
plugin_service_general_read (xmlNode *tree, ErrorInfo **ret_error)
{
	PluginService *service = NULL;
	PluginServiceGeneral *service_general;

	g_return_val_if_fail (tree != NULL, NULL);
	g_return_val_if_fail (ret_error != NULL, NULL);

	*ret_error = NULL;
	service = g_new (PluginService, 1);
	plugin_service_init (service, PLUGIN_SERVICE_GENERAL);
	service_general = &service->t.general;
	service_general->plugin_func_init = NULL;
	service_general->plugin_func_can_deactivate = NULL;
	service_general->plugin_func_cleanup = NULL;

	return service;
}

static void
plugin_service_general_free (PluginService *service)
{
	g_return_if_fail (service != NULL);
}

static void
plugin_service_general_initialize (PluginService *service, ErrorInfo **ret_error)
{
	ErrorInfo *error;

	g_return_if_fail (service != NULL);
	g_return_if_fail (service->service_type == PLUGIN_SERVICE_GENERAL);
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	plugin_service_load (service, &error);
	if (error == NULL) {
		PluginServiceGeneral *service_general = &service->t.general;

		g_return_if_fail (service_general->plugin_func_init != NULL);
		service_general->plugin_func_init (service, &error);
		if (error != NULL) {
			*ret_error = error_info_new_str_with_details (
			             _("Initializing function inside plugin returned error."),
			             error);
		}
	} else {
		*ret_error = error_info_new_str_with_details (
		             _("Error while loading plugin service."),
		             error);
	}
}

static gboolean
plugin_service_general_can_deactivate (PluginService *service)
{
	PluginServiceGeneral *service_general;

	g_return_val_if_fail (service != NULL, FALSE);
	g_return_val_if_fail (service->service_type == PLUGIN_SERVICE_GENERAL, FALSE);

	service_general = &service->t.general;
	g_return_val_if_fail (service_general->plugin_func_can_deactivate != NULL, FALSE);
	return service_general->plugin_func_can_deactivate (service);
}

static void
plugin_service_general_cleanup (PluginService *service, ErrorInfo **ret_error)
{
	PluginServiceGeneral *service_general;
	ErrorInfo *error;

	g_return_if_fail (service != NULL);
	g_return_if_fail (service->service_type == PLUGIN_SERVICE_GENERAL);
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	service_general = &service->t.general;
	g_return_if_fail (service_general->plugin_func_cleanup != NULL);
	service_general->plugin_func_cleanup (service, &error);
	if (error != NULL) {
		*ret_error = error_info_new_str_with_details (
		             _("Cleanup function inside plugin returned error."),
		             error);
	}
}

/*
 * FileOpener
 */

typedef struct _GnumPluginFileOpener GnumPluginFileOpener;
typedef struct _GnumPluginFileOpenerClass GnumPluginFileOpenerClass;

#define TYPE_GNUM_PLUGIN_FILE_OPENER             (gnum_plugin_file_opener_get_type ())
#define GNUM_PLUGIN_FILE_OPENER(obj)             (GTK_CHECK_CAST ((obj), TYPE_GNUM_PLUGIN_FILE_OPENER, GnumPluginFileOpener))
#define GNUM_PLUGIN_FILE_OPENER_CLASS(klass)     (GTK_CHECK_CLASS_CAST ((klass), TYPE_GNUM_PLUGIN_FILE_OPENER, GnumPluginFileOpenerClass))
#define IS_GNUM_PLUGIN_FILE_OPENER(obj)          (GTK_CHECK_TYPE ((obj), TYPE_GNUM_PLUGIN_FILE_OPENER))
#define IS_GNUM_PLUGIN_FILE_OPENER_CLASS(klass)  (GTK_CHECK_CLASS_TYPE ((klass), TYPE_GNUM_PLUGIN_FILE_OPENER))

GtkType gnum_plugin_file_opener_get_type (void);

struct _GnumPluginFileOpenerClass {
	GnumFileOpenerClass parent_class;
};

struct _GnumPluginFileOpener {
	GnumFileOpener parent;

	PluginService *service;
};

static void
gnum_plugin_file_opener_init (GnumPluginFileOpener *fo)
{
	fo->service = NULL;
}

static gboolean
gnum_plugin_file_opener_probe (GnumFileOpener const *fo, const gchar *file_name)
{
	GnumPluginFileOpener *pfo;
	PluginServiceFileOpener *service_file_opener;
	gboolean file_name_matches;
	gchar *base_file_name = g_basename (file_name);
	GList *l;

	g_return_val_if_fail (IS_GNUM_PLUGIN_FILE_OPENER (fo), FALSE);
	g_return_val_if_fail (file_name != NULL, FALSE);

	pfo = GNUM_PLUGIN_FILE_OPENER (fo);
	service_file_opener = &pfo->service->t.file_opener;
	file_name_matches = FALSE;
	for (l = service_file_opener->file_patterns; l != NULL; l = l->next) {
		InputFilePattern *pattern;

		pattern = (InputFilePattern *) l->data;
		switch (pattern->pattern_type) {
		case FILE_PATTERN_SHELL: {
			if (pattern->case_sensitive) {
				if (fnmatch (pattern->value, base_file_name, FNM_PATHNAME) == 0) {
					file_name_matches = TRUE;
				}
			} else {
				gchar *pattern_str, *name_str;

				pattern_str = g_alloca (strlen (pattern->value) + 1);
				name_str = g_alloca (strlen (base_file_name) + 1);
				g_strdown (strcpy (pattern_str, pattern->value));
				g_strdown (strcpy (name_str, base_file_name));
				if (fnmatch (pattern_str, name_str, FNM_PATHNAME) == 0) {
					file_name_matches = TRUE;
				}
			}
			break;
		}
		case FILE_PATTERN_REGEXP:
			g_warning ("Not implemented");
			break;
		default:
			g_assert_not_reached ();
		}	
	}

	if (service_file_opener->has_probe &&
	    (service_file_opener->file_patterns == NULL || file_name_matches)) {
		ErrorInfo *ignored_error;

		plugin_service_load (pfo->service, &ignored_error);
		if (ignored_error == NULL) {
			g_return_val_if_fail (service_file_opener->plugin_func_file_probe != NULL, FALSE);
			return service_file_opener->plugin_func_file_probe (fo, pfo->service, file_name);
		} else {
			error_info_print (ignored_error);
			error_info_free (ignored_error);
			return FALSE;
		}
	} else {
		return file_name_matches;
	}
}

static void
gnum_plugin_file_opener_open (GnumFileOpener const *fo, IOContext *io_context,
                              WorkbookView *wbv, const gchar *file_name)

{
	GnumPluginFileOpener *pfo;
	PluginServiceFileOpener *service_file_opener;
	ErrorInfo *error;

	g_return_if_fail (IS_GNUM_PLUGIN_FILE_OPENER (fo));
	g_return_if_fail (file_name != NULL);

	pfo = GNUM_PLUGIN_FILE_OPENER (fo);
	service_file_opener = &pfo->service->t.file_opener;
	plugin_service_load (pfo->service, &error);
	if (error == NULL) {
		g_return_if_fail (service_file_opener->plugin_func_file_open != NULL);
		service_file_opener->plugin_func_file_open (fo, pfo->service, io_context, wbv, file_name);
		if (!gnumeric_io_error_occurred (io_context)) {
			if (service_file_opener->save_info != NULL) {
				InputFileSaveInfo *save_info;

				save_info = service_file_opener->save_info;
				if (save_info->saver_id_str[0] == '\0') {
					workbook_set_saveinfo (wb_view_workbook (wbv), file_name,
					                       save_info->format_level, NULL);
				} else {
					GHashTable *file_savers_hash;
					GnumFileSaver *saver;

					file_savers_hash = plugin_info_peek_services_data (pfo->service->plugin)->file_savers_hash;
					saver = (GnumFileSaver *) g_hash_table_lookup (file_savers_hash,
					                                           save_info->saver_id_str);
					if (saver != NULL) {
						workbook_set_saveinfo (wb_view_workbook (wbv), file_name,
						                       save_info->format_level, saver);
					}
				}
			}
		}
	} else {
		gnumeric_io_error_info_set (io_context, error);
		gnumeric_io_error_info_push (io_context, error_info_new_str (
		                            _("Error while reading file.")));
	}
}

static void
gnum_plugin_file_opener_class_init (GnumPluginFileOpenerClass *klass)
{
	GnumFileOpenerClass *gnum_file_opener_klass = GNUM_FILE_OPENER_CLASS (klass);

	gnum_file_opener_klass->probe = gnum_plugin_file_opener_probe;
	gnum_file_opener_klass->open = gnum_plugin_file_opener_open;
}

E_MAKE_TYPE (gnum_plugin_file_opener, "GnumPluginFileOpener", GnumPluginFileOpener, \
             gnum_plugin_file_opener_class_init, gnum_plugin_file_opener_init, \
             TYPE_GNUM_FILE_OPENER)

static GnumPluginFileOpener *
gnum_plugin_file_opener_new (PluginService *service)
{
	GnumPluginFileOpener *fo;
	PluginServiceFileOpener *service_file_opener;
	gchar *opener_id;

	service_file_opener = &service->t.file_opener;
	opener_id = g_strdup_printf ("%s:%s",
	                             plugin_info_peek_id (service->plugin),
	                             service_file_opener->id);
	fo = GNUM_PLUGIN_FILE_OPENER (gtk_type_new (TYPE_GNUM_PLUGIN_FILE_OPENER));
	gnum_file_opener_setup (GNUM_FILE_OPENER (fo), opener_id,
	                        service_file_opener->description,
	                        NULL, NULL);
	fo->service = service;
	g_free (opener_id);

	return fo;
}

static void
input_file_pattern_free (gpointer data)
{
	InputFilePattern *pattern = (InputFilePattern *) data;

	g_return_if_fail (pattern != NULL);

	g_free (pattern->value);
	g_free (pattern);
}

static void
input_file_saver_info_free (InputFileSaveInfo *saver_info)
{
	g_free (saver_info->saver_id_str);
	g_free (saver_info);
}

static InputFileSaveInfo *
input_file_save_info_read (xmlNode *tree)
{
	InputFileSaveInfo *save_info = NULL;
	gchar *saver_id_str, *format_level_str;

	g_return_val_if_fail (tree != NULL, NULL);
	g_return_val_if_fail (strcmp (tree->name, "save_info") == 0, NULL);

	saver_id_str = e_xml_get_string_prop_by_name (tree, "saver_id");
	format_level_str = e_xml_get_string_prop_by_name (tree, "format_level");

	save_info = g_new (InputFileSaveInfo, 1);
	save_info->saver_id_str = saver_id_str != NULL ? saver_id_str : g_strdup ("");
	save_info->format_level = parse_format_level_str (format_level_str, FILE_FL_MANUAL);

	g_free (format_level_str);		

	return save_info;
}

static PluginService *
plugin_service_file_opener_read (xmlNode *tree, ErrorInfo **ret_error)
{
	PluginService *service = NULL;
	gchar *id_str;
	guint priority;
	gboolean has_probe, can_open, can_import;
	xmlNode *information_node;
	gchar *description;

	g_return_val_if_fail (tree != NULL, NULL);
	g_return_val_if_fail (ret_error != NULL, NULL);

	*ret_error = NULL;
	id_str = e_xml_get_string_prop_by_name (tree, "id");
	priority = e_xml_get_uint_prop_by_name_with_default (tree, "priority", 50);
	priority = MIN (priority, 100);
	has_probe = e_xml_get_bool_prop_by_name_with_default (tree, "probe", TRUE);
	can_open = e_xml_get_bool_prop_by_name_with_default (tree, "open", TRUE);
	can_import = e_xml_get_bool_prop_by_name_with_default (tree, "import", FALSE);
	information_node = e_xml_get_child_by_name_by_lang_list (tree, "information", NULL);
	if (information_node != NULL) {
		description = e_xml_get_string_prop_by_name (information_node, "description");
	} else {
		description = NULL;
	}
	if (id_str != NULL && description != NULL) {
		GList *file_patterns = NULL;
		xmlNode *file_patterns_node, *save_info_node, *node;
		PluginServiceFileOpener *service_file_opener;
		InputFileSaveInfo *save_info;

		file_patterns_node = e_xml_get_child_by_name (tree, "file_patterns");
		if (file_patterns_node != NULL) {
			for (node = file_patterns_node->xmlChildrenNode; node != NULL; node = node->next) {
				InputFilePattern *file_pattern;
				gchar *value, *type_str;

				if (strcmp (node->name, "file_pattern") != 0 ||
				    (value = e_xml_get_string_prop_by_name (node, "value")) == NULL) {
					continue;
				}
				type_str = e_xml_get_string_prop_by_name (node, "type");
				file_pattern = g_new (InputFilePattern, 1);
				file_pattern->value = value;
				if (type_str == NULL) {
					file_pattern->pattern_type = FILE_PATTERN_SHELL;
				} else if (g_strcasecmp (type_str, "shell_pattern") == 0) {
					file_pattern->pattern_type = FILE_PATTERN_SHELL;
					file_pattern->case_sensitive = e_xml_get_bool_prop_by_name_with_default (
					                               node, "case_sensitive", FALSE);
				} else if (g_strcasecmp (type_str, "regexp") == 0) {
					file_pattern->pattern_type = FILE_PATTERN_REGEXP;
				} else {
					file_pattern->pattern_type = FILE_PATTERN_SHELL;
				}
				g_free (type_str);
				file_patterns = g_list_prepend (file_patterns, file_pattern);
			}
		}
		file_patterns = g_list_reverse (file_patterns);

		save_info_node = e_xml_get_child_by_name (tree, "save_info");
		if (save_info_node != NULL) {
			save_info = input_file_save_info_read (save_info_node);
		} else {
			save_info = NULL;
		}

		service = g_new (PluginService, 1);
		plugin_service_init (service, PLUGIN_SERVICE_FILE_OPENER);
		service_file_opener = &service->t.file_opener;
		service_file_opener->id = id_str;
		service_file_opener->priority = priority;
		service_file_opener->has_probe = has_probe;
		service_file_opener->can_open = can_open;
		service_file_opener->can_import = can_import;
		service_file_opener->description = description;
		service_file_opener->file_patterns = file_patterns;
		service_file_opener->save_info = save_info;
		service_file_opener->opener = NULL;
		service_file_opener->plugin_func_file_probe = NULL;
		service_file_opener->plugin_func_file_open = NULL;
	} else {
		if (id_str == NULL) {
			*ret_error = error_info_new_str (_("File opener has no id"));
		} else {
			*ret_error = error_info_new_printf (
			             _("File opener with id=\"%s\" has no description"),
			             id_str);
		}

		g_free (id_str);
		g_free (description);
	}

	return service;
}

static void
plugin_service_file_opener_free (PluginService *service)
{
	PluginServiceFileOpener *service_file_opener;

	g_return_if_fail (service != NULL);

	service_file_opener = &service->t.file_opener;
	g_free (service_file_opener->id);
	g_free (service_file_opener->description);
	g_list_free_custom (service_file_opener->file_patterns, &input_file_pattern_free);
	input_file_saver_info_free (service_file_opener->save_info);
}

static void
plugin_service_file_opener_initialize (PluginService *service, ErrorInfo **ret_error)
{
	PluginServiceFileOpener *service_file_opener;

	g_return_if_fail (service != NULL);
	g_return_if_fail (service->service_type == PLUGIN_SERVICE_FILE_OPENER);
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	service_file_opener = &service->t.file_opener;
	service_file_opener->opener = GNUM_FILE_OPENER (gnum_plugin_file_opener_new (service));
	if (service_file_opener->can_open) {
		register_file_opener (service_file_opener->opener,
		                      service_file_opener->priority);
	}
	if (service_file_opener->can_import) {
		register_file_opener_as_importer (service_file_opener->opener);
	}
}

static gboolean
plugin_service_file_opener_can_deactivate (PluginService *service)
{
	g_return_val_if_fail (service != NULL, FALSE);
	g_return_val_if_fail (service->service_type == PLUGIN_SERVICE_FILE_OPENER, FALSE);

	return TRUE;
}

static void
plugin_service_file_opener_cleanup (PluginService *service, ErrorInfo **ret_error)
{
	PluginServiceFileOpener *service_file_opener;

	g_assert (service != NULL);
	g_assert (service->service_type == PLUGIN_SERVICE_FILE_OPENER);
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	service_file_opener = &service->t.file_opener;
	if (service_file_opener->can_open) {
		unregister_file_opener (service_file_opener->opener);
	}
	if (service_file_opener->can_import) {
		unregister_file_opener_as_importer (service_file_opener->opener);
	}
}

/*
 * FileSaver
 */

typedef struct _GnumPluginFileSaver GnumPluginFileSaver;
typedef struct _GnumPluginFileSaverClass GnumPluginFileSaverClass;

#define TYPE_GNUM_PLUGIN_FILE_SAVER             (gnum_plugin_file_saver_get_type ())
#define GNUM_PLUGIN_FILE_SAVER(obj)             (GTK_CHECK_CAST ((obj), TYPE_GNUM_PLUGIN_FILE_SAVER, GnumPluginFileSaver))
#define GNUM_PLUGIN_FILE_SAVER_CLASS(klass)     (GTK_CHECK_CLASS_CAST ((klass), TYPE_GNUM_PLUGIN_FILE_SAVER, GnumPluginFileSaverClass))
#define IS_GNUM_PLUGIN_FILE_SAVER(obj)          (GTK_CHECK_TYPE ((obj), TYPE_GNUM_PLUGIN_FILE_SAVER))
#define IS_GNUM_PLUGIN_FILE_SAVER_CLASS(klass)  (GTK_CHECK_CLASS_TYPE ((klass), TYPE_GNUM_PLUGIN_FILE_SAVER))

GtkType gnum_plugin_file_saver_get_type (void);

struct _GnumPluginFileSaverClass {
	GnumFileSaverClass parent_class;
};

struct _GnumPluginFileSaver {
	GnumFileSaver parent;

	PluginService *service;
};

static void
gnum_plugin_file_saver_init (GnumPluginFileSaver *fs)
{
	fs->service = NULL;
}

static void
gnum_plugin_file_saver_save (GnumFileSaver const *fs, IOContext *io_context,
                             WorkbookView *wbv, const gchar *file_name)
{
	GnumPluginFileSaver *pfs;
	PluginServiceFileSaver *service_file_saver;
	ErrorInfo *error;

	g_return_if_fail (IS_GNUM_PLUGIN_FILE_SAVER (fs));
	g_return_if_fail (file_name != NULL);

	pfs = GNUM_PLUGIN_FILE_SAVER (fs);
	service_file_saver = &pfs->service->t.file_saver;
	plugin_service_load (pfs->service, &error);
	if (error == NULL) {
		g_return_if_fail (service_file_saver->plugin_func_file_save != NULL);
		service_file_saver->plugin_func_file_save (fs, pfs->service, io_context, wbv, file_name);
	} else {
		gnumeric_io_error_info_set (io_context, error);
		gnumeric_io_error_info_push (io_context, error_info_new_str (
		                             _("Error while saving file.")));
	}
}

static void
gnum_plugin_file_saver_class_init (GnumPluginFileSaverClass *klass)
{
	GnumFileSaverClass *gnum_file_saver_klass = GNUM_FILE_SAVER_CLASS (klass);

	gnum_file_saver_klass->save = gnum_plugin_file_saver_save;
}

E_MAKE_TYPE (gnum_plugin_file_saver, "GnumPluginFileSaver", GnumPluginFileSaver, \
             gnum_plugin_file_saver_class_init, gnum_plugin_file_saver_init, \
             TYPE_GNUM_FILE_SAVER)

static GnumPluginFileSaver *
gnum_plugin_file_saver_new (PluginService *service)
{
	GnumPluginFileSaver *fs;
	PluginServiceFileSaver *service_file_saver;
	gchar *saver_id;

	service_file_saver = &service->t.file_saver;
	saver_id = g_strdup_printf ("%s:%s",
	                             plugin_info_peek_id (service->plugin),
	                             service_file_saver->id);
	fs = GNUM_PLUGIN_FILE_SAVER (gtk_type_new (TYPE_GNUM_PLUGIN_FILE_SAVER));
	gnum_file_saver_setup (GNUM_FILE_SAVER (fs), saver_id,
	                       service_file_saver->file_extension,
	                       service_file_saver->description,
	                       service_file_saver->format_level,
	                       NULL);
	fs->service = service;
	g_free (saver_id);

	return fs;
}

static PluginService *
plugin_service_file_saver_read (xmlNode *tree, ErrorInfo **ret_error)
{
	PluginService *service = NULL;
	gchar *id_str;
	gchar *file_extension;
	xmlNode *information_node;
	gchar *description;
	gchar *format_level_str;

	g_return_val_if_fail (tree != NULL, NULL);
	g_return_val_if_fail (ret_error != NULL, NULL);

	*ret_error = NULL;
	id_str = e_xml_get_string_prop_by_name (tree, "id");
	file_extension = e_xml_get_string_prop_by_name (tree, "file_extension");
	format_level_str = e_xml_get_string_prop_by_name (tree, "format_level");
	information_node = e_xml_get_child_by_name_by_lang_list (tree, "information", NULL);
	if (information_node != NULL) {
		description = e_xml_get_string_prop_by_name (information_node, "description");
	} else {
		description = NULL;
	}
	if (id_str != NULL && description != NULL) {
		PluginServiceFileSaver *service_file_saver;
		
		service = g_new (PluginService, 1);
		plugin_service_init (service, PLUGIN_SERVICE_FILE_SAVER);
		service_file_saver = &service->t.file_saver;
		service_file_saver->id = id_str;
		service_file_saver->file_extension = file_extension;
		service_file_saver->description = description;
		service_file_saver->format_level = parse_format_level_str (format_level_str,
		                                                           FILE_FL_WRITE_ONLY);
		service_file_saver->default_saver_priority = e_xml_get_integer_prop_by_name_with_default (
		                                             tree, "default_saver_priority", -1);
		service_file_saver->plugin_func_file_save = NULL;

		g_free (format_level_str);
	} else {
		if (id_str == NULL) {
			*ret_error = error_info_new_str (_("File saver has no id"));
		} else {
			*ret_error = error_info_new_printf (
			             _("File saver with id=\"%s\" has no description"),
			             id_str);
		}
		g_free (id_str);
		g_free (file_extension);
		g_free (format_level_str);
		g_free (description);
	}

	return service;	
}

static void
plugin_service_file_saver_free (PluginService *service)
{
	PluginServiceFileSaver *service_file_saver;

	g_return_if_fail (service != NULL);

	service_file_saver = &service->t.file_saver;
	g_free (service_file_saver->id);
	g_free (service_file_saver->file_extension);
	g_free (service_file_saver->description);
}

static void
plugin_service_file_saver_initialize (PluginService *service, ErrorInfo **ret_error)
{
	PluginServiceFileSaver *service_file_saver;
	GHashTable *file_savers_hash;

	g_return_if_fail (service != NULL);
	g_return_if_fail (service->service_type == PLUGIN_SERVICE_FILE_SAVER);
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	service_file_saver = &service->t.file_saver;
	service_file_saver->saver = GNUM_FILE_SAVER (gnum_plugin_file_saver_new (service));
	if (service_file_saver->default_saver_priority < 0) {
		register_file_saver (service_file_saver->saver);
	} else {
		register_file_saver_as_default (service_file_saver->saver,
		                                service_file_saver->default_saver_priority);
	}
	file_savers_hash = plugin_info_peek_services_data (service->plugin)->file_savers_hash;
	g_assert (g_hash_table_lookup (file_savers_hash, service_file_saver->id) == NULL);
	g_hash_table_insert (file_savers_hash, service_file_saver->id, service_file_saver->saver);
}

static gboolean
plugin_service_file_saver_can_deactivate (PluginService *service)
{
	g_return_val_if_fail (service != NULL, FALSE);
	g_return_val_if_fail (service->service_type == PLUGIN_SERVICE_FILE_SAVER, FALSE);

	return TRUE;
}

static void
plugin_service_file_saver_cleanup (PluginService *service, ErrorInfo **ret_error)
{
	PluginServiceFileSaver *service_file_saver;
	GHashTable *file_savers_hash;

	g_return_if_fail (service != NULL);
	g_return_if_fail (service->service_type == PLUGIN_SERVICE_FILE_SAVER);
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	service_file_saver = &service->t.file_saver;
	file_savers_hash = plugin_info_peek_services_data (service->plugin)->file_savers_hash;
	g_hash_table_remove (file_savers_hash, service_file_saver->id);
	unregister_file_saver (service_file_saver->saver);
}

/*
 * FunctionGroup
 */

static PluginService *
plugin_service_function_group_read (xmlNode *tree, ErrorInfo **ret_error)
{
	PluginService *service = NULL;
	xmlNode *category_node, *translated_category_node, *functions_node;
	gchar *group_id, *category_name, *translated_category_name;
	GList *function_name_list = NULL;

	g_return_val_if_fail (tree != NULL, NULL);
	g_return_val_if_fail (ret_error != NULL, NULL);

	*ret_error = NULL;
	group_id = e_xml_get_string_prop_by_name (tree, "id");
	category_node = e_xml_get_child_by_name_no_lang (tree, "category");
	if (category_node != NULL) {
		category_name = e_xml_get_string_prop_by_name (category_node, "name");
	} else {
		category_name = NULL;
	}
	translated_category_node = e_xml_get_child_by_name_by_lang_list (tree, "category", NULL);
	if (translated_category_node != NULL) {
		gchar *lang;

		lang = e_xml_get_string_prop_by_name (translated_category_node, "xml:lang");
		if (lang != NULL) {
			translated_category_name = e_xml_get_string_prop_by_name (translated_category_node, "name");
			g_free (lang);
		} else {
			translated_category_name = NULL;
		}
	} else {
		translated_category_name = NULL;
	}
	functions_node = e_xml_get_child_by_name (tree, "functions");
	if (functions_node != NULL) {
		xmlNode *node;

		for (node = functions_node->xmlChildrenNode; node != NULL; node = node->next) {
			gchar *func_name;

			if (strcmp (node->name, "function") != 0 ||
			    (func_name = e_xml_get_string_prop_by_name (node, "name")) == NULL) {
				continue;
			}
			function_name_list = g_list_prepend (function_name_list, func_name);
		}
		function_name_list = g_list_reverse (function_name_list);
	}
	if (group_id != NULL && category_name != NULL && function_name_list != NULL) {
		PluginServiceFunctionGroup *service_function_group;

		service = g_new (PluginService, 1);
		plugin_service_init (service, PLUGIN_SERVICE_FUNCTION_GROUP);
		service_function_group = &service->t.function_group;
		service_function_group->group_id = group_id;
		service_function_group->category_name = category_name;
		service_function_group->translated_category_name = translated_category_name;
		service_function_group->function_name_list = function_name_list;
	} else {
		GList *error_list = NULL;

		if (group_id == NULL) {
			error_list = g_list_prepend (error_list,
			                             error_info_new_str (
			                             _("Missing function group id.")));
		}	
		if (category_name == NULL) {
			error_list = g_list_prepend (error_list,
			                             error_info_new_str (
			                             _("Missing function category name.")));
		}	
		if (function_name_list == NULL) {
			error_list = g_list_prepend (error_list,
			                             error_info_new_str (
			                             _("Missing function category name.")));
		}	
		error_list = g_list_reverse (error_list);
		*ret_error = error_info_new_from_error_list (error_list);

		g_free (group_id);
		g_free (category_name);
		g_free (translated_category_name);
		e_free_string_list (function_name_list);
	}

	return service;
}

static void
plugin_service_function_group_free (PluginService *service)
{
	PluginServiceFunctionGroup *service_function_group;

	g_return_if_fail (service != NULL);

	service_function_group = &service->t.function_group;
	g_free (service_function_group->group_id);
	g_free (service_function_group->category_name);
	g_free (service_function_group->translated_category_name);
	e_free_string_list (service_function_group->function_name_list);
}

static gboolean
plugin_service_function_group_get_full_info_callback (FunctionDefinition *fn_def,
                                                      gchar **args_ptr,
                                                      gchar **arg_names_ptr,
                                                      gchar ***help_ptr,
                                                      FunctionArgs **fn_args_ptr,
                                                      FunctionNodes **fn_nodes_ptr)
{
	PluginService *service;
	PluginServiceFunctionGroup *service_function_group;
	ErrorInfo *error;

	g_return_val_if_fail (fn_def != NULL, FALSE);

	service = (PluginService *) function_def_get_user_data (fn_def);
	service_function_group = &service->t.function_group;
	plugin_service_load (service, &error);
	if (error == NULL) {
		gchar *args, *arg_names;
		gchar **help;
		FunctionArgs *fn_args;
		FunctionNodes *fn_nodes;

		if (service_function_group->plugin_func_get_full_function_info (
		    service, function_def_get_name (fn_def),
		    &args, &arg_names, &help, &fn_args, &fn_nodes)) {
			*args_ptr = args;
			*arg_names_ptr = arg_names;
			*help_ptr = help;
			*fn_args_ptr = fn_args;
			*fn_nodes_ptr = fn_nodes;
			return TRUE;
		} else {
			return FALSE;
		}
	} else {
		error_info_print (error);
		error_info_free (error);
		return FALSE;
	}
}

static void
plugin_service_function_group_initialize (PluginService *service, ErrorInfo **ret_error)
{
	PluginServiceFunctionGroup *service_function_group;

	GList *l;

	g_return_if_fail (service != NULL);
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	service_function_group = &service->t.function_group;
	service_function_group->category = function_get_category_with_translation (
	                                   service_function_group->category_name,
	                                   service_function_group->translated_category_name);
	for (l = service_function_group->function_name_list; l != NULL; l = l->next) {
		FunctionDefinition *fn_def;

		fn_def = function_add_name_only (service_function_group->category, (gchar *) l->data,
		                                 &plugin_service_function_group_get_full_info_callback);
		function_def_set_user_data (fn_def, (gpointer) service);
	}
}

static gboolean
plugin_service_function_group_can_deactivate (PluginService *service)
{
	PluginServiceFunctionGroup *service_function_group;
	GList *l;
	gboolean is_in_use = FALSE;

	g_return_val_if_fail (service != NULL, FALSE);
	g_return_val_if_fail (service->service_type == PLUGIN_SERVICE_FUNCTION_GROUP, FALSE);

	service_function_group = &service->t.function_group;
	for (l = service_function_group->function_name_list; l != NULL; l = l->next) {
		FunctionDefinition *fn_def;

		fn_def = func_lookup_by_name ((gchar *) l->data, NULL);
		g_assert (fn_def != NULL);
		if (func_get_ref_count (fn_def) != 0) {
			is_in_use = TRUE;
			break;
		}
	}

	return !is_in_use;
}

static void
plugin_service_function_group_cleanup (PluginService *service, ErrorInfo **ret_error)
{
	PluginServiceFunctionGroup *service_function_group;
	GList *l;

	g_return_if_fail (service != NULL);
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	service_function_group = &service->t.function_group;
	for (l = service_function_group->function_name_list; l != NULL; l = l->next) {
		function_remove (service_function_group->category, (gchar *) l->data);
	}
}

/*
 * PluginLoader
 */

static PluginService *
plugin_service_plugin_loader_read (xmlNode *tree, ErrorInfo **ret_error)
{
	PluginService *service = NULL;
	gchar *loader_id;

	g_return_val_if_fail (tree != NULL, NULL);
	g_return_val_if_fail (ret_error != NULL, NULL);

	*ret_error = NULL;
	loader_id = e_xml_get_string_prop_by_name (tree, "id");
	if (loader_id != NULL) {
		PluginServicePluginLoader *service_plugin_loader;

		service = g_new (PluginService, 1);
		plugin_service_init (service, PLUGIN_SERVICE_PLUGIN_LOADER);
		service_plugin_loader = &service->t.plugin_loader;
		service_plugin_loader->loader_id = loader_id;
	} else {
		*ret_error = error_info_new_str (
		             _("Missing loader id."));
	}

	return service;
}

static void
plugin_service_plugin_loader_free (PluginService *service)
{
	PluginServicePluginLoader *service_plugin_loader;

	g_return_if_fail (service != NULL);

	service_plugin_loader = &service->t.plugin_loader;
	g_free (service_plugin_loader->loader_id);
}

static GtkType
plugin_service_plugin_loader_get_type_callback (gpointer callback_data, ErrorInfo **ret_error)
{
	PluginService *service;
	PluginServicePluginLoader *service_plugin_loader;
	ErrorInfo *error;
	GtkType loader_type;

	*ret_error = NULL;
	service = (PluginService *) callback_data;
	service_plugin_loader = &service->t.plugin_loader;
	plugin_service_load (service, &error);
	if (error == NULL) {
		loader_type = service_plugin_loader->plugin_func_get_loader_type (
		                                     service, &error);
		if (error == NULL) {
			return loader_type;
		} else {
			*ret_error = error;
			return (GtkType) 0;
		}
	} else {
		*ret_error = error_info_new_str_with_details (
		             _("Error while loading plugin service."),
		             error);
		return (GtkType) 0;
	}
}

static void
plugin_service_plugin_loader_initialize (PluginService *service, ErrorInfo **ret_error)
{
	PluginServicePluginLoader *service_plugin_loader;

	g_return_if_fail (service != NULL);
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	service_plugin_loader = &service->t.plugin_loader;
	plugin_loader_register_id_only (service_plugin_loader->loader_id,
	                                &plugin_service_plugin_loader_get_type_callback,
	                                (gpointer) service);
}

static gboolean
plugin_service_plugin_loader_can_deactivate (PluginService *service)
{
	g_return_val_if_fail (service != NULL, FALSE);
	g_return_val_if_fail (service->service_type == PLUGIN_SERVICE_PLUGIN_LOADER, FALSE);

	return FALSE;
}

static void
plugin_service_plugin_loader_cleanup (PluginService *service, ErrorInfo **ret_error)
{
	g_return_if_fail (service != NULL);
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
}

/* ---------------------------------------------------------------------- */

static void
plugin_service_init (PluginService *service, PluginServiceType service_type)
{
	g_return_if_fail (service != NULL);

	service->service_type = service_type;
	service->is_active = FALSE;
	service->is_loaded = FALSE;
	service->plugin = NULL;
	service->loader_data = NULL;
}

static void
plugin_service_load (PluginService *service, ErrorInfo **ret_error)
{
	ErrorInfo *error;

	g_return_if_fail (service != NULL);
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	if (service->is_loaded) {
		return;
	}
	plugin_load_service (service->plugin, service, &error);
	if (error == NULL) {
		service->is_loaded = TRUE;
	} else {
		*ret_error = error;
	}
}

static void
plugin_service_unload (PluginService *service, ErrorInfo **ret_error)
{
	ErrorInfo *error;

	g_return_if_fail (service != NULL);
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	if (!service->is_loaded) {
		return;
	}
	plugin_unload_service (service->plugin, service, &error);
	if (error == NULL) {
		service->is_loaded = FALSE;
	} else {
		*ret_error = error;
	}
}

PluginService *
plugin_service_read (xmlNode *tree, ErrorInfo **ret_error)
{
	PluginService *service = NULL;
	gchar *type_str;
	ErrorInfo *service_error;

	g_return_val_if_fail (tree != NULL, NULL);
	g_return_val_if_fail (strcmp (tree->name, "service") == 0, NULL);
	g_return_val_if_fail (ret_error != NULL, NULL);

	*ret_error = NULL;
	type_str = e_xml_get_string_prop_by_name (tree, "type");
	if (g_strcasecmp (type_str, "general") == 0) {
		service = plugin_service_general_read (tree, &service_error);
	} else if (g_strcasecmp (type_str, "file_opener") == 0) {
		service = plugin_service_file_opener_read (tree, &service_error);
	} else if (g_strcasecmp (type_str, "file_saver") == 0) {
		service = plugin_service_file_saver_read (tree, &service_error);
	} else if (g_strcasecmp (type_str, "function_group") == 0) {
		service = plugin_service_function_group_read (tree, &service_error);
	} else if (g_strcasecmp (type_str, "plugin_loader") == 0) {
		service = plugin_service_plugin_loader_read (tree, &service_error);
	} else {
		service = plugin_service_general_read (tree, &service_error);
	}
	if (service != NULL) {
		g_assert (service_error == NULL);
	} else {
		*ret_error = error_info_new_printf (
		             _("Error while reading service of type \"%s\"."),
		             type_str);
		error_info_add_details (*ret_error, service_error);
	}
	g_free (type_str);

	return service;
}

void
plugin_service_free (PluginService *service)
{
	g_return_if_fail (service != NULL);

	switch (service->service_type) {
	case PLUGIN_SERVICE_GENERAL:
		plugin_service_general_free (service);
		break;
	case PLUGIN_SERVICE_FILE_OPENER:
		plugin_service_file_opener_free (service);
		break;
	case PLUGIN_SERVICE_FILE_SAVER:
		plugin_service_file_saver_free (service);
		break;
	case PLUGIN_SERVICE_FUNCTION_GROUP:
		plugin_service_function_group_free (service);
		break;
	case PLUGIN_SERVICE_PLUGIN_LOADER:
		plugin_service_plugin_loader_free (service);
		break;
	default:
		g_assert_not_reached ();
	}
}

void
plugin_service_set_plugin (PluginService *service, PluginInfo *plugin)
{
	g_return_if_fail (service != NULL);
	g_return_if_fail (plugin != NULL);

	service->plugin = plugin;
}

void
plugin_service_set_loader_data (PluginService *service, gpointer loader_data)
{
	g_return_if_fail (service != NULL);
	g_return_if_fail (loader_data != NULL);

	service->loader_data = loader_data;
}

void
plugin_service_clear_loader_data (PluginService *service)
{
	g_return_if_fail (service != NULL);

	service->loader_data = NULL;
}

gpointer
plugin_service_get_loader_data (PluginService *service)
{
	g_return_val_if_fail (service != NULL, NULL);

	return service->loader_data;
}

void
plugin_service_activate (PluginService *service, ErrorInfo **ret_error)
{
	ErrorInfo *error;

	g_return_if_fail (service != NULL);
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	if (service->is_active) {
		return;
	}
	switch (service->service_type) {
	case PLUGIN_SERVICE_GENERAL:
		plugin_service_general_initialize (service, &error);
		break;
	case PLUGIN_SERVICE_FILE_OPENER:
		plugin_service_file_opener_initialize (service, &error);
		break;
	case PLUGIN_SERVICE_FILE_SAVER:
		plugin_service_file_saver_initialize (service, &error);
		break;
	case PLUGIN_SERVICE_FUNCTION_GROUP:
		plugin_service_function_group_initialize (service, &error);
		break;
	case PLUGIN_SERVICE_PLUGIN_LOADER:
		plugin_service_plugin_loader_initialize (service, &error);
		break;
	default:
		g_assert_not_reached ();
	}
	if (error == NULL) {
#ifdef PLUGIN_ALWAYS_LOAD
		plugin_service_load (service, &error);
		if (error != NULL) {
			*ret_error = error_info_new_str_with_details (
			             _("Error while loading plugin service."),
			             error);
		}
#endif
		service->is_active = TRUE;
	} else {
		*ret_error = error;
	}
}

gboolean
plugin_service_can_deactivate (PluginService *service)
{
	gboolean can_deactivate = FALSE;

	g_return_val_if_fail (service != NULL, FALSE);

	switch (service->service_type) {
	case PLUGIN_SERVICE_GENERAL:
		can_deactivate = plugin_service_general_can_deactivate (service);
		break;
	case PLUGIN_SERVICE_FILE_OPENER:
		can_deactivate = plugin_service_file_opener_can_deactivate (service);
		break;
	case PLUGIN_SERVICE_FILE_SAVER:
		can_deactivate = plugin_service_file_saver_can_deactivate (service);
		break;
	case PLUGIN_SERVICE_FUNCTION_GROUP:
		can_deactivate = plugin_service_function_group_can_deactivate (service);
		break;
	case PLUGIN_SERVICE_PLUGIN_LOADER:
		can_deactivate = plugin_service_plugin_loader_can_deactivate (service);
		break;
	default:
		g_assert_not_reached ();
	}

	return can_deactivate;
}

void
plugin_service_deactivate (PluginService *service, ErrorInfo **ret_error)
{
	ErrorInfo *error;

	g_return_if_fail (service != NULL);
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	if (!service->is_active) {
		return;
	}
	switch (service->service_type) {
	case PLUGIN_SERVICE_GENERAL:
		plugin_service_general_cleanup (service, &error);
		break;
	case PLUGIN_SERVICE_FILE_OPENER:
		plugin_service_file_opener_cleanup (service, &error);
		break;
	case PLUGIN_SERVICE_FILE_SAVER:
		plugin_service_file_saver_cleanup (service, &error);
		break;
	case PLUGIN_SERVICE_FUNCTION_GROUP:
		plugin_service_function_group_cleanup (service, &error);
		break;
	case PLUGIN_SERVICE_PLUGIN_LOADER:
		plugin_service_plugin_loader_cleanup (service, &error);
		break;
	default:
		g_assert_not_reached ();
	}
	if (error == NULL) {
		ErrorInfo *ignored_error;

		service->is_active = FALSE;
		plugin_service_unload (service, &ignored_error);
		error_info_free (ignored_error);
	} else {
		*ret_error = error;
	}
}

PluginServicesData *
plugin_services_data_new (void)
{
	PluginServicesData *services_data;

	services_data = g_new (PluginServicesData, 1);
	services_data->file_savers_hash = g_hash_table_new (&g_str_hash, &g_str_equal);

	return services_data;
}

void
plugin_services_data_free (PluginServicesData *services_data)
{
	g_return_if_fail (services_data != NULL);

	g_hash_table_destroy (services_data->file_savers_hash);
	g_free (services_data);
}

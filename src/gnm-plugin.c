/*
 * gnm-plugin.c: Plugin services - reading XML info, activating, etc.
 *                   (everything independent of plugin loading method)
 *
 * Author: Zbigniew Chyla (cyba@gnome.pl)
 */

#include <gnumeric-config.h>
#include <gutils.h>
#include <tools/gnm-solver.h>
#include <func.h>
#include <gnm-plugin.h>
#include <gnumeric-conf.h>
#include <application.h>

#include <goffice/goffice.h>
#include <gsf/gsf-impl-utils.h>
#include <gsf/gsf-input-stdio.h>
#include <gsf/gsf-input-memory.h>
#include <glib/gi18n-lib.h>
#include <string.h>

#define CXML2C(s) ((char const *)(s))
#define CC2XML(s) ((xmlChar const *)(s))

static char *
xml2c (xmlChar *src)
{
	char *dst = g_strdup (CXML2C (src));
	xmlFree (src);
	return dst;
}

typedef GOPluginServiceSimpleClass GnmPluginServiceFunctionGroupClass;
struct GnmPluginServiceFunctionGroup_ {
	GOPluginServiceSimple	base;

	gchar *category_name, *translated_category_name;
	GSList *function_name_list;

	GnmFuncGroup *func_group;
	GnmPluginServiceFunctionGroupCallbacks cbs;
	char *tdomain;
};

static void
plugin_service_function_group_finalize (GObject *obj)
{
	GnmPluginServiceFunctionGroup *sfg = GNM_PLUGIN_SERVICE_FUNCTION_GROUP (obj);
	GObjectClass *parent_class;

	g_free (sfg->category_name);
	sfg->category_name = NULL;

	g_free (sfg->translated_category_name);
	sfg->translated_category_name = NULL;

	g_slist_free_full (sfg->function_name_list, g_free);
	sfg->function_name_list = NULL;

	g_free (sfg->tdomain);
	sfg->tdomain = NULL;

	parent_class = g_type_class_peek (GO_TYPE_PLUGIN_SERVICE);
	parent_class->finalize (obj);
}

static void
plugin_service_function_group_read_xml (GOPluginService *service, xmlNode *tree, GOErrorInfo **ret_error)
{
	xmlNode *category_node, *translated_category_node, *functions_node;
	gchar *category_name, *translated_category_name;
	GSList *function_name_list = NULL;
	gchar *tdomain = NULL;

	GO_INIT_RET_ERROR_INFO (ret_error);
	category_node = go_xml_get_child_by_name_no_lang (tree, "category");
	category_name = category_node
		? xml2c (xmlNodeGetContent (category_node))
		: NULL;

	translated_category_node = go_xml_get_child_by_name_by_lang (tree, "category");
	if (translated_category_node != NULL) {
		xmlChar *lang;

		lang = go_xml_node_get_cstr (translated_category_node, "lang");
		if (lang != NULL) {
			translated_category_name =
				xml2c (xmlNodeGetContent (translated_category_node));
			xmlFree (lang);
		} else {
			translated_category_name = NULL;
		}
	} else {
		translated_category_name = NULL;
	}
	functions_node = go_xml_get_child_by_name (tree, CC2XML ("functions"));
	if (functions_node != NULL) {
		xmlNode *node;

		tdomain = xml2c (go_xml_node_get_cstr (functions_node, "textdomain"));

		for (node = functions_node->xmlChildrenNode; node != NULL; node = node->next) {
			gchar *func_name;

			if (strcmp (CXML2C (node->name), "function") != 0)
				continue;

			func_name = xml2c (go_xml_node_get_cstr (node, "name"));
			if (!func_name)
				continue;

			GO_SLIST_PREPEND (function_name_list, func_name);
		}
		GO_SLIST_REVERSE (function_name_list);
	}
	if (category_name != NULL && function_name_list != NULL) {
		GnmPluginServiceFunctionGroup *sfg = GNM_PLUGIN_SERVICE_FUNCTION_GROUP (service);

		sfg->category_name = category_name;
		sfg->translated_category_name = translated_category_name;
		sfg->function_name_list = function_name_list;
		sfg->tdomain = tdomain;
	} else {
		GSList *error_list = NULL;

		if (category_name == NULL) {
			GO_SLIST_PREPEND (error_list, go_error_info_new_str (
				_("Missing function category name.")));
		}
		if (function_name_list == NULL) {
			GO_SLIST_PREPEND (error_list, go_error_info_new_str (
				_("Function group is empty.")));
		}
		GO_SLIST_REVERSE (error_list);
		*ret_error = go_error_info_new_from_error_list (error_list);

		g_free (category_name);
		g_free (translated_category_name);
		g_slist_free_full (function_name_list, g_free);

		g_free (tdomain);
	}
}

static void
plugin_service_function_group_func_ref_notify (GnmFunc *fn_def,
					       G_GNUC_UNUSED GParamSpec *pspec,
					       GOPlugin *plugin)
{
	if (gnm_func_get_in_use (fn_def))
		go_plugin_use_ref (plugin);
	else
		go_plugin_use_unref (plugin);
}

static void
plugin_service_function_group_func_load_stub (GnmFunc *fn_def,
					      GOPluginService *service)
{
	GnmPluginServiceFunctionGroup *sfg = GNM_PLUGIN_SERVICE_FUNCTION_GROUP (service);
	GOErrorInfo *error = NULL;

	g_return_if_fail (fn_def != NULL);

	go_plugin_service_load (service, &error);
	if (error != NULL) {
		go_error_info_print (error);
		go_error_info_free (error);
		return;
	}

	if (!sfg->cbs.load_stub) {
                error = go_error_info_new_printf (_("No load_stub method.\n"));
		go_error_info_print (error);
		go_error_info_free (error);
		return;
	}

	sfg->cbs.load_stub (service, fn_def);
}

static void
delayed_ref_notify (GOPlugin *plugin, GnmFunc *fd)
{
	g_signal_handlers_disconnect_by_func (plugin,
					      G_CALLBACK (delayed_ref_notify),
					      fd);

	/* We cannot do this until after the plugin has been activated.  */
	plugin_service_function_group_func_ref_notify (fd, NULL, plugin);
}

static void
plugin_service_function_group_activate (GOPluginService *service, GOErrorInfo **ret_error)
{
	GnmPluginServiceFunctionGroup *sfg =
		GNM_PLUGIN_SERVICE_FUNCTION_GROUP (service);
	GOPlugin *plugin = go_plugin_service_get_plugin (service);
	GSList *l;

	GO_INIT_RET_ERROR_INFO (ret_error);
	sfg->func_group = gnm_func_group_fetch (sfg->category_name,
						sfg->translated_category_name);
	if (gnm_debug_flag ("plugin-func"))
		g_printerr ("Activating group %s\n", sfg->category_name);

	for (l = sfg->function_name_list; l; l = l->next) {
		const char *fname = l->data;
		GnmFunc *func = gnm_func_lookup_or_add_placeholder (fname);

		gnm_func_set_stub (func);
		gnm_func_set_translation_domain (func, sfg->tdomain);
		gnm_func_set_function_group (func, sfg->func_group);
		// Clear localized_name so we can deduce the proper name.
		//gnm_func_set_localized_name (func, NULL);

		g_signal_connect
			(func, "notify::in-use",
			 G_CALLBACK (plugin_service_function_group_func_ref_notify),
			 plugin);

		g_signal_connect
			(func, "load-stub",
			 G_CALLBACK (plugin_service_function_group_func_load_stub),
			 service);

		if (gnm_func_get_in_use (func))
			g_signal_connect (plugin,
					  "state_changed",
					  G_CALLBACK (delayed_ref_notify),
					  func);
	}
	service->is_active = TRUE;
}

static void
plugin_service_function_group_deactivate (GOPluginService *service, GOErrorInfo **ret_error)
{
	GnmPluginServiceFunctionGroup *sfg = GNM_PLUGIN_SERVICE_FUNCTION_GROUP (service);
	GOPlugin *plugin = go_plugin_service_get_plugin (service);
	GSList *l;

	if (gnm_debug_flag ("plugin-func"))
		g_printerr ("Deactivating group %s\n", sfg->category_name);

	GO_INIT_RET_ERROR_INFO (ret_error);

	for (l = sfg->function_name_list; l; l = l->next) {
		const char *fname = l->data;
		GnmFunc *func = gnm_func_lookup (fname, NULL);

		// This should not happen, but if it were to, having a handler
		// of some other object is not going to be good.
		if (gnm_func_get_in_use (func))
			g_signal_handlers_disconnect_by_func
				(plugin, G_CALLBACK (delayed_ref_notify), func);

		// Someone else might hold a ref so make sure the object
		// becomes inaccessible via gnm_func_lookup
		gnm_func_dispose (func);

		g_object_unref (func);
	}
	service->is_active = FALSE;
}

static char *
plugin_service_function_group_get_description (GOPluginService *service)
{
	GnmPluginServiceFunctionGroup *sfg = GNM_PLUGIN_SERVICE_FUNCTION_GROUP (service);
	int n_functions;
	char const *category_name;

	n_functions = g_slist_length (sfg->function_name_list);
	category_name = sfg->translated_category_name != NULL
		? sfg->translated_category_name
		: sfg->category_name;

	return g_strdup_printf (ngettext (
			"%d function in category \"%s\"",
			"Group of %d functions in category \"%s\"",
			n_functions),
		n_functions, category_name);
}

static void
plugin_service_function_group_init (GnmPluginServiceFunctionGroup *s)
{
	GO_PLUGIN_SERVICE (s)->cbs_ptr = &s->cbs;
	s->category_name = NULL;
	s->translated_category_name = NULL;
	s->function_name_list = NULL;
	s->func_group = NULL;
	s->tdomain = NULL;
}

static void
plugin_service_function_group_class_init (GObjectClass *gobject_class)
{
	GOPluginServiceClass *plugin_service_class = GO_PLUGIN_SERVICE_CLASS (gobject_class);

	gobject_class->finalize		= plugin_service_function_group_finalize;
	plugin_service_class->read_xml	= plugin_service_function_group_read_xml;
	plugin_service_class->activate	= plugin_service_function_group_activate;
	plugin_service_class->deactivate = plugin_service_function_group_deactivate;
	plugin_service_class->get_description = plugin_service_function_group_get_description;
}

GSF_CLASS (GnmPluginServiceFunctionGroup, gnm_plugin_service_function_group,
           plugin_service_function_group_class_init, plugin_service_function_group_init,
           GO_TYPE_PLUGIN_SERVICE_SIMPLE)

/****************************************************************************/

/*
 * PluginServiceUI
 */
typedef GOPluginServiceSimpleClass PluginServiceUIClass;
struct GnmPluginServiceUI_ {
	GOPluginServiceSimple base;

	char *file_name;
	GSList *actions;

	gpointer layout_id;
	GnmPluginServiceUICallbacks cbs;
};

static void
plugin_service_ui_init (PluginServiceUI *s)
{
	GO_PLUGIN_SERVICE (s)->cbs_ptr = &s->cbs;
	s->file_name = NULL;
	s->actions = NULL;
	s->layout_id = NULL;
	s->cbs.plugin_func_exec_action = NULL;
}

static void
plugin_service_ui_finalize (GObject *obj)
{
	PluginServiceUI *service_ui = GNM_PLUGIN_SERVICE_UI (obj);
	GObjectClass *parent_class;

	g_free (service_ui->file_name);
	service_ui->file_name = NULL;
	g_slist_free_full (service_ui->actions, (GDestroyNotify)gnm_action_unref);
	service_ui->actions = NULL;

	parent_class = g_type_class_peek (GO_TYPE_PLUGIN_SERVICE);
	parent_class->finalize (obj);
}

static void
cb_ui_service_activate (GnmAction const *action, WorkbookControl *wbc, GOPluginService *service)
{
	GOErrorInfo *load_error = NULL;

	go_plugin_service_load (service, &load_error);
	if (load_error == NULL) {
		PluginServiceUI *service_ui = GNM_PLUGIN_SERVICE_UI (service);
		GOErrorInfo *ignored_error = NULL;

		g_return_if_fail (service_ui->cbs.plugin_func_exec_action != NULL);
		service_ui->cbs.plugin_func_exec_action (
			service, action, wbc, &ignored_error);
		if (ignored_error != NULL) {
			go_error_info_print (ignored_error);
			go_error_info_free (ignored_error);
		}
	} else {
		go_error_info_print (load_error);
		go_error_info_free (load_error);
	}
}

static void
plugin_service_ui_read_xml (GOPluginService *service, xmlNode *tree, GOErrorInfo **ret_error)
{
	PluginServiceUI *service_ui = GNM_PLUGIN_SERVICE_UI (service);
	xmlChar *file_name;
	xmlNode *verbs_node;
	GSList *actions = NULL;

	GO_INIT_RET_ERROR_INFO (ret_error);
	file_name = xml2c (go_xml_node_get_cstr (tree, "file"));
	if (file_name == NULL) {
		*ret_error = go_error_info_new_str (
		             _("Missing file name."));
		return;
	}
	verbs_node = go_xml_get_child_by_name (tree, "actions");
	if (verbs_node != NULL) {
		xmlNode *ptr, *label_node;
		xmlChar *name, *icon;
		gchar *label;
		gboolean always_available;
		GnmAction *action;

		for (ptr = verbs_node->xmlChildrenNode; ptr != NULL; ptr = ptr->next) {
			if (xmlIsBlankNode (ptr) || ptr->name == NULL ||
			    strcmp (CXML2C (ptr->name), "action"))
				continue;
			name  = go_xml_node_get_cstr (ptr, "name");
/*			label = go_xml_node_get_cstr (ptr, "label");*/
/*****************************************************************************************/
			label_node = go_xml_get_child_by_name_no_lang (ptr, "label");
			label = label_node
				? xml2c (xmlNodeGetContent (label_node))
				: NULL;

			label_node = go_xml_get_child_by_name_by_lang (ptr, "label");
			if (label_node != NULL) {
				gchar *lang;

				lang = go_xml_node_get_cstr (label_node, "lang");
				if (lang != NULL) {
					label = xml2c (xmlNodeGetContent (label_node));
					xmlFree (lang);
				}
			}
/*****************************************************************************************/
			icon  = go_xml_node_get_cstr (ptr, "icon");
			if (!go_xml_node_get_bool (ptr, "always_available", &always_available))
				always_available = FALSE;
			action = gnm_action_new (name, label, icon, always_available,
						 (GnmActionHandler) cb_ui_service_activate,
						 service, NULL);
			if (NULL != name) xmlFree (name);
			g_free (label);
			if (NULL != icon) xmlFree (icon);
			if (NULL != action)
				GO_SLIST_PREPEND (actions, action);
		}
	}
	GO_SLIST_REVERSE (actions);

	service_ui->file_name = file_name;
	service_ui->actions = actions;
}

static void
plugin_service_ui_activate (GOPluginService *service, GOErrorInfo **ret_error)
{
	PluginServiceUI *service_ui = GNM_PLUGIN_SERVICE_UI (service);
	const char *uifile = service_ui->file_name;
	char *xml_ui, *group_name;
	char const *tdomain;
	GError *error = NULL;
	GsfInput *src;
	size_t len;

	GO_INIT_RET_ERROR_INFO (ret_error);

	if (strncmp (uifile, "res:", 4) == 0) {
		size_t len;
		gconstpointer data = go_rsm_lookup (uifile + 4, &len);
		src = data
			? gsf_input_memory_new (data, len, FALSE)
			: NULL;
	} else if (strncmp (uifile, "data:", 5) == 0) {
		const char *data = uifile + 5;
		src = gsf_input_memory_new (data, strlen (data), FALSE);
	} else {
		char *full_file_name = g_path_is_absolute (uifile)
			? g_strdup (uifile)
			: g_build_filename
			(go_plugin_get_dir_name (service->plugin),
			 uifile,
			 NULL);
		src = gsf_input_stdio_new (full_file_name, &error);
		g_free (full_file_name);
	}
	if (!src)
		goto err;

	src = gsf_input_uncompress (src);
	len = gsf_input_size (src);
	xml_ui = g_strndup (gsf_input_read (src, len, NULL), len);
	if (!xml_ui)
		goto err;

	tdomain = go_plugin_get_textdomain (service->plugin);
	group_name = g_strconcat (go_plugin_get_id (service->plugin), service->id, NULL);
	service_ui->layout_id = gnm_app_add_extra_ui (group_name,
		service_ui->actions,
		xml_ui, tdomain);
	g_free (group_name);
	g_free (xml_ui);
	g_object_unref (src);
	service->is_active = TRUE;
	return;

err:
	*ret_error = go_error_info_new_printf
		(_("Cannot read UI description from %s: %s"),
		 uifile,
		 error ? error->message : "?");
	g_clear_error (&error);
	if (src)
		g_object_unref (src);
}

static void
plugin_service_ui_deactivate (GOPluginService *service, GOErrorInfo **ret_error)
{
	PluginServiceUI *service_ui = GNM_PLUGIN_SERVICE_UI (service);

	GO_INIT_RET_ERROR_INFO (ret_error);
	gnm_app_remove_extra_ui (service_ui->layout_id);
	service_ui->layout_id = NULL;
	service->is_active = FALSE;
}

static char *
plugin_service_ui_get_description (GOPluginService *service)
{
	PluginServiceUI *service_ui = GNM_PLUGIN_SERVICE_UI (service);
	int n_actions;

	n_actions = g_slist_length (service_ui->actions);
	return g_strdup_printf (
		ngettext (
/* xgettext : %d gives the number of actions. This is input to ngettext. */
			"User interface with %d action",
			"User interface with %d actions",
			n_actions),
		n_actions);
}

static void
plugin_service_ui_class_init (GObjectClass *gobject_class)
{
	GOPluginServiceClass *plugin_service_class = GO_PLUGIN_SERVICE_CLASS (gobject_class);

	gobject_class->finalize = plugin_service_ui_finalize;
	plugin_service_class->read_xml = plugin_service_ui_read_xml;
	plugin_service_class->activate = plugin_service_ui_activate;
	plugin_service_class->deactivate = plugin_service_ui_deactivate;
	plugin_service_class->get_description = plugin_service_ui_get_description;
}

GSF_CLASS (PluginServiceUI, gnm_plugin_service_ui,
           plugin_service_ui_class_init, plugin_service_ui_init,
           GO_TYPE_PLUGIN_SERVICE_SIMPLE)

/****************************************************************************/

/*
 * PluginServiceSolver
 */
typedef GOPluginServiceClass PluginServiceSolverClass;
struct GnmPluginServiceSolver_ {
	GOPluginService base;

	GnmSolverFactory *factory;

	GnmPluginServiceSolverCallbacks cbs;
};

static GnmSolver *
cb_load_and_create (GnmSolverFactory *factory, GnmSolverParameters *param,
		    gpointer data)
{
	PluginServiceSolver *ssol =
		g_object_get_data (G_OBJECT (factory), "ssol");
	GOPluginService *service = GO_PLUGIN_SERVICE (ssol);
	GOErrorInfo *ignored_error = NULL;
	GnmSolver *res;

	go_plugin_service_load (service, &ignored_error);
	if (ignored_error != NULL) {
		go_error_info_print (ignored_error);
		go_error_info_free (ignored_error);
		return NULL;
	}

	res = ssol->cbs.creator (factory, param, data);
	if (res) {
		go_plugin_use_ref (service->plugin);
		g_object_set_data_full (G_OBJECT (res),
					"plugin-use", service->plugin,
					(GDestroyNotify)go_plugin_use_unref);
	}

	return res;
}

static gboolean
cb_load_and_functional (GnmSolverFactory *factory,
			WBCGtk *wbcg,
			gpointer data)
{
	PluginServiceSolver *ssol =
		g_object_get_data (G_OBJECT (factory), "ssol");
	GOPluginService *service = GO_PLUGIN_SERVICE (ssol);
	GOErrorInfo *ignored_error = NULL;
	GnmSolverFactoryFunctional functional;

	go_plugin_service_load (service, &ignored_error);
	if (ignored_error != NULL) {
		go_error_info_print (ignored_error);
		go_error_info_free (ignored_error);
		return FALSE;
	}

	functional = ssol->cbs.functional;
	return (functional == NULL || functional (factory, wbcg, data));
}

static void
plugin_service_solver_init (PluginServiceSolver *ssol)
{
	GO_PLUGIN_SERVICE (ssol)->cbs_ptr = &ssol->cbs;
	ssol->factory = NULL;
	ssol->cbs.creator = NULL;
}

static void
plugin_service_solver_finalize (GObject *obj)
{
	PluginServiceSolver *ssol = GNM_PLUGIN_SERVICE_SOLVER (obj);
	GObjectClass *parent_class;

	if (ssol->factory)
		g_object_unref (ssol->factory);

	parent_class = g_type_class_peek (GO_TYPE_PLUGIN_SERVICE);
	parent_class->finalize (obj);
}

static void
plugin_service_solver_read_xml (GOPluginService *service, xmlNode *tree,
				GOErrorInfo **ret_error)
{
	PluginServiceSolver *ssol = GNM_PLUGIN_SERVICE_SOLVER (service);
	xmlChar *s_id, *s_name, *s_type;
	GnmSolverModelType type = GNM_SOLVER_LP;
	xmlNode *information_node;

	GO_INIT_RET_ERROR_INFO (ret_error);

	s_type = go_xml_node_get_cstr (tree, "model_type");
	if (s_type && strcmp (CXML2C (s_type), "mip") == 0)
		type = GNM_SOLVER_LP;
	else if (s_type && strcmp (CXML2C (s_type), "qp") == 0)
		type = GNM_SOLVER_QP;
	else if (s_type && strcmp (CXML2C (s_type), "nlp") == 0)
		type = GNM_SOLVER_NLP;
	else {
		*ret_error = go_error_info_new_str (_("Invalid solver model type."));
		return;
	}
	xmlFree (s_type);

	s_id = go_xml_node_get_cstr (tree, "id");

	s_name = NULL;
	information_node = go_xml_get_child_by_name (tree, "information");
	if (information_node != NULL) {
		xmlNode *node =
			go_xml_get_child_by_name_by_lang (information_node,
							  "description");
		if (node != NULL) {
			s_name = xmlNodeGetContent (node);
		}
	}

	if (!s_id || !s_name) {
		*ret_error = go_error_info_new_str (_("Missing fields in plugin file"));
	} else {
		ssol->factory = gnm_solver_factory_new (CXML2C (s_id),
							CXML2C (s_name),
							type,
							cb_load_and_create,
							cb_load_and_functional,
							NULL,
							NULL);
		g_object_set_data (G_OBJECT (ssol->factory), "ssol", ssol);
	}
	xmlFree (s_id);
	xmlFree (s_name);
	if (*ret_error)
		return;

	/* More? */
}

static void
plugin_service_solver_activate (GOPluginService *service, GOErrorInfo **ret_error)
{
	PluginServiceSolver *ssol = GNM_PLUGIN_SERVICE_SOLVER (service);

	GO_INIT_RET_ERROR_INFO (ret_error);
	gnm_solver_db_register (ssol->factory);
	service->is_active = TRUE;
}

static void
plugin_service_solver_deactivate (GOPluginService *service, GOErrorInfo **ret_error)
{
	PluginServiceSolver *ssol = GNM_PLUGIN_SERVICE_SOLVER (service);

	GO_INIT_RET_ERROR_INFO (ret_error);
	gnm_solver_db_unregister (ssol->factory);
	service->is_active = FALSE;
}

static char *
plugin_service_solver_get_description (GOPluginService *service)
{
	PluginServiceSolver *ssol = GNM_PLUGIN_SERVICE_SOLVER (service);
	return g_strdup_printf (_("Solver Algorithm %s"),
				ssol->factory->name);
}

static void
plugin_service_solver_class_init (GObjectClass *gobject_class)
{
	GOPluginServiceClass *plugin_service_class = GO_PLUGIN_SERVICE_CLASS (gobject_class);

	gobject_class->finalize = plugin_service_solver_finalize;
	plugin_service_class->read_xml = plugin_service_solver_read_xml;
	plugin_service_class->activate = plugin_service_solver_activate;
	plugin_service_class->deactivate = plugin_service_solver_deactivate;
	plugin_service_class->get_description = plugin_service_solver_get_description;
}

GSF_CLASS (PluginServiceSolver, gnm_plugin_service_solver,
           plugin_service_solver_class_init, plugin_service_solver_init,
           GO_TYPE_PLUGIN_SERVICE)

/****************************************************************************/


typedef GOPluginLoaderModule	  GnmPluginLoaderModule;
typedef GOPluginLoaderModuleClass GnmPluginLoaderModuleClass;

/*
 * Service - function_group
 */
typedef struct {
	GnmFuncDescriptor *module_fn_info_array;
	GHashTable *function_indices;
} ServiceLoaderDataFunctionGroup;

static void
function_group_loader_data_free (gpointer data)
{
	ServiceLoaderDataFunctionGroup *ld = data;

	g_hash_table_destroy (ld->function_indices);
	g_free (ld);
}

static void
gnm_plugin_loader_module_func_load_stub (GOPluginService *service,
					 GnmFunc *func)
{
	ServiceLoaderDataFunctionGroup *loader_data;
	gpointer index_ptr;
	GnmFuncDescriptor *desc;
	const char *name;

	g_return_if_fail (GNM_IS_PLUGIN_SERVICE_FUNCTION_GROUP (service));
	g_return_if_fail (GNM_IS_FUNC (func));

	name = gnm_func_get_name (func, FALSE);
	loader_data = g_object_get_data (G_OBJECT (service), "loader_data");
	if (!g_hash_table_lookup_extended (loader_data->function_indices,
					   (gpointer)name,
					   NULL, &index_ptr))
		return; // Failed

	desc = loader_data->module_fn_info_array + GPOINTER_TO_INT (index_ptr);
	gnm_func_set_from_desc (func, desc);
}

static void
gnm_plugin_loader_module_load_service_function_group (GOPluginLoader  *loader,
						      GOPluginService *service,
						      GOErrorInfo **ret_error)
{
	GnmPluginLoaderModule *loader_module = GNM_PLUGIN_LOADER_MODULE (loader);
	gchar *fn_info_array_name;
	GnmFuncDescriptor *module_fn_info_array = NULL;

	g_return_if_fail (GNM_IS_PLUGIN_SERVICE_FUNCTION_GROUP (service));

	GO_INIT_RET_ERROR_INFO (ret_error);
	fn_info_array_name = g_strconcat (
		go_plugin_service_get_id (service), "_functions", NULL);
	g_module_symbol (loader_module->handle, fn_info_array_name, (gpointer) &module_fn_info_array);
	if (module_fn_info_array != NULL) {
		GnmPluginServiceFunctionGroupCallbacks *cbs;
		ServiceLoaderDataFunctionGroup *loader_data;
		gint i;

		cbs = go_plugin_service_get_cbs (service);
		cbs->load_stub = &gnm_plugin_loader_module_func_load_stub;

		loader_data = g_new (ServiceLoaderDataFunctionGroup, 1);
		loader_data->module_fn_info_array = module_fn_info_array;
		loader_data->function_indices = g_hash_table_new (&g_str_hash, &g_str_equal);
		for (i = 0; module_fn_info_array[i].name != NULL; i++) {
			g_hash_table_insert (loader_data->function_indices,
			                     (gpointer) module_fn_info_array[i].name,
			                     GINT_TO_POINTER (i));
		}
		g_object_set_data_full (
			G_OBJECT (service), "loader_data", loader_data, function_group_loader_data_free);
	} else {
		*ret_error = go_error_info_new_printf (
		             _("Module file \"%s\" has invalid format."),
		             loader_module->module_file_name);
		go_error_info_add_details (*ret_error,
					go_error_info_new_printf (
					_("File doesn't contain \"%s\" array."),
					fn_info_array_name));
	}
	g_free (fn_info_array_name);
}

/*
 * Service - ui
 */

typedef struct {
	GnmModulePluginUIActions *module_ui_actions_array;
	GHashTable *ui_actions_hash;
} ServiceLoaderDataUI;

static void
ui_loader_data_free (gpointer data)
{
	ServiceLoaderDataUI *ld = data;

	g_hash_table_destroy (ld->ui_actions_hash);
	g_free (ld);
}

static void
gnm_plugin_loader_module_func_exec_action (GOPluginService *service,
					   GnmAction const *action,
					   WorkbookControl *wbc,
					   GOErrorInfo **ret_error)
{
	ServiceLoaderDataUI *loader_data;
	gpointer action_index_ptr;
	int action_index;

	g_return_if_fail (GNM_IS_PLUGIN_SERVICE_UI (service));

	GO_INIT_RET_ERROR_INFO (ret_error);
	loader_data = g_object_get_data (G_OBJECT (service), "loader_data");
	if (!g_hash_table_lookup_extended (loader_data->ui_actions_hash, action->id,
	                                   NULL, &action_index_ptr)) {
		*ret_error = go_error_info_new_printf (_("Unknown action: %s"), action->id);
		return;
	}
	action_index = GPOINTER_TO_INT (action_index_ptr);
	if (NULL != loader_data->module_ui_actions_array [action_index].handler)
		(*loader_data->module_ui_actions_array [action_index].handler) (action, wbc);
}

static void
gnm_plugin_loader_module_load_service_ui (GOPluginLoader *loader,
					  GOPluginService *service,
					  GOErrorInfo **ret_error)
{
	GnmPluginLoaderModule *loader_module = GNM_PLUGIN_LOADER_MODULE (loader);
	char *ui_actions_array_name;
	GnmModulePluginUIActions *module_ui_actions_array = NULL;
	GnmPluginServiceUICallbacks *cbs;
	ServiceLoaderDataUI *loader_data;
	gint i;

	g_return_if_fail (GNM_IS_PLUGIN_SERVICE_UI (service));

	GO_INIT_RET_ERROR_INFO (ret_error);
	ui_actions_array_name = g_strconcat (
		go_plugin_service_get_id (service), "_ui_actions", NULL);
	g_module_symbol (loader_module->handle, ui_actions_array_name, (gpointer) &module_ui_actions_array);
	if (module_ui_actions_array == NULL) {
		*ret_error = go_error_info_new_printf (
			_("Module file \"%s\" has invalid format."),
			loader_module->module_file_name);
		go_error_info_add_details (*ret_error, go_error_info_new_printf (
			_("File doesn't contain \"%s\" array."), ui_actions_array_name));
		g_free (ui_actions_array_name);
		return;
	}
	g_free (ui_actions_array_name);

	cbs = go_plugin_service_get_cbs (service);
	cbs->plugin_func_exec_action = gnm_plugin_loader_module_func_exec_action;

	loader_data = g_new (ServiceLoaderDataUI, 1);
	loader_data->module_ui_actions_array = module_ui_actions_array;
	loader_data->ui_actions_hash = g_hash_table_new (g_str_hash, g_str_equal);
	for (i = 0; module_ui_actions_array[i].name != NULL; i++)
		g_hash_table_insert (loader_data->ui_actions_hash,
			(gpointer) module_ui_actions_array[i].name,
			GINT_TO_POINTER (i));
	g_object_set_data_full (G_OBJECT (service),
		"loader_data", loader_data, ui_loader_data_free);
}

static void
gnm_plugin_loader_module_load_service_solver (GOPluginLoader *loader,
					      GOPluginService *service,
					      GOErrorInfo **ret_error)
{
	GnmPluginLoaderModule *loader_module =
		GNM_PLUGIN_LOADER_MODULE (loader);
	GnmPluginServiceSolverCallbacks *cbs;
	char *symname;
	GnmSolverCreator creator;
	GnmSolverFactoryFunctional functional;

	g_return_if_fail (GNM_IS_PLUGIN_SERVICE_SOLVER (service));

	GO_INIT_RET_ERROR_INFO (ret_error);

	symname = g_strconcat (go_plugin_service_get_id (service),
			       "_solver_factory",
			       NULL);
	g_module_symbol (loader_module->handle, symname, (gpointer)&creator);
	g_free (symname);
	if (!creator) {
		*ret_error = go_error_info_new_printf (
			_("Module file \"%s\" has invalid format."),
			loader_module->module_file_name);
		return;
	}

	symname = g_strconcat (go_plugin_service_get_id (service),
			       "_solver_factory_functional",
			       NULL);
	g_module_symbol (loader_module->handle, symname, (gpointer)&functional);
	g_free (symname);

	cbs = go_plugin_service_get_cbs (service);
	cbs->creator = creator;
	cbs->functional = functional;
}

static gboolean
gplm_service_load (GOPluginLoader *l, GOPluginService *s, GOErrorInfo **err)
{
	if (GNM_IS_PLUGIN_SERVICE_FUNCTION_GROUP (s))
		gnm_plugin_loader_module_load_service_function_group (l, s, err);
	else if (GNM_IS_PLUGIN_SERVICE_UI (s))
		gnm_plugin_loader_module_load_service_ui (l, s, err);
	else if (GNM_IS_PLUGIN_SERVICE_SOLVER (s))
		gnm_plugin_loader_module_load_service_solver (l, s, err);
	else
		return FALSE;
	return TRUE;
}

static gboolean
gplm_service_unload (GOPluginLoader *l, GOPluginService *s, GOErrorInfo **err)
{
	if (GNM_IS_PLUGIN_SERVICE_FUNCTION_GROUP (s)) {
		GnmPluginServiceFunctionGroupCallbacks *cbs = go_plugin_service_get_cbs (s);
		cbs->load_stub = NULL;
	} else if (GNM_IS_PLUGIN_SERVICE_UI (s)) {
		GnmPluginServiceUICallbacks *cbs = go_plugin_service_get_cbs (s);
		cbs->plugin_func_exec_action = NULL;
	} else if (GNM_IS_PLUGIN_SERVICE_SOLVER (s)) {
		GnmPluginServiceSolverCallbacks *cbs =
			go_plugin_service_get_cbs (s);
		cbs->creator = NULL;
		cbs->functional = NULL;
	} else
		return FALSE;
	return TRUE;
}

static void
go_plugin_loader_module_iface_init (GOPluginLoaderClass *iface)
{
	iface->service_load   = gplm_service_load;
	iface->service_unload = gplm_service_unload;
}

GSF_CLASS_FULL (GnmPluginLoaderModule, gnm_plugin_loader_module,
           NULL, NULL, NULL, NULL,
           NULL, GO_TYPE_PLUGIN_LOADER_MODULE, 0,
	   GSF_INTERFACE (go_plugin_loader_module_iface_init, GO_TYPE_PLUGIN_LOADER))

/****************************************************************************/

/**
 * gnm_plugins_service_init: (skip)
 */
void
gnm_plugins_service_init (void)
{
	go_plugin_service_define ("function_group",
		&gnm_plugin_service_function_group_get_type);
	go_plugin_service_define ("ui",
		&gnm_plugin_service_ui_get_type);
	go_plugin_service_define ("solver",
		&gnm_plugin_service_solver_get_type);
	go_plugin_loader_module_register_version ("gnumeric", GNM_VERSION_FULL);
}


void
gnm_plugins_init (GOCmdContext *context)
{
	char const *env_var;
	GSList *dir_list = go_slist_create (
		g_build_filename (gnm_sys_lib_dir (), PLUGIN_SUBDIR, NULL),
	        g_strdup (gnm_sys_extern_plugin_dir ()),
		(gnm_usr_dir (TRUE) == NULL ? NULL :
			g_build_filename (gnm_usr_dir (TRUE), PLUGIN_SUBDIR, NULL)),
		NULL);
	dir_list = g_slist_concat (dir_list,
				   go_string_slist_copy (gnm_conf_get_plugins_extra_dirs ()));

	env_var = g_getenv ("GNUMERIC_PLUGIN_PATH");
	if (env_var != NULL)
		GO_SLIST_CONCAT (dir_list, go_strsplit_to_slist (env_var, G_SEARCHPATH_SEPARATOR));

	go_plugins_init (GO_CMD_CONTEXT (context),
			 gnm_conf_get_plugins_file_states (),
			 gnm_conf_get_plugins_active (),
			 dir_list,
			 gnm_conf_get_plugins_activate_newplugins (),
			 gnm_plugin_loader_module_get_type ());
}

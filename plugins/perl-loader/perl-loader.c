#include <EXTERN.h>
#include <perl.h>
#include "perl-gnumeric.h"
#undef _
#define _perl_dirty dirty
#undef dirty

#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include <gnumeric.h>

#include <stdlib.h>
#include <glib.h>
#include "application.h"
#include "workbook.h"
#include "sheet.h"
#include "workbook-view.h"
#include "workbook-control-gui.h"
#include "gui-util.h"
#include "value.h"
#include "expr.h"
#include "expr-impl.h"
#include <goffice/app/io-context.h>
#include <goffice/app/go-plugin.h>
#include <goffice/app/go-plugin-service.h>
#include <goffice/app/go-plugin-loader.h>
#include <goffice/app/module-plugin-defs.h>
#include "perl-loader.h"

#define dirty _perl_dirty
#undef _perl_dirty

PerlInterpreter* gnm_perl_interp;
PerlInterpreter* my_perl;
extern void xs_init(void);

typedef struct _GnmPluginLoaderPerl GnmPluginLoaderPerl;

struct _GnmPluginLoaderPerl {
	GOPluginLoader loader;
	gchar* module_name;
};

typedef struct {
	GnmPluginLoaderClass parent_class;
} GnmPluginLoaderPerlClass;

static GObjectClass *parent_class = NULL;

static void gplp_set_attributes (GOPluginLoader *loader, GHashTable* attrs, ErrorInfo **ret_error);
static void gplp_load_base (GOPluginLoader *loader, ErrorInfo **ret_error);
static void gplp_load_service_function_group (GOPluginLoader *loader, GOPluginService *service, ErrorInfo **ret_error);
static gboolean gplp_func_desc_load (GOPluginService *service, char const *name, GnmFuncDescriptor *res);
static GnmValue* call_perl_function_args (FunctionEvalInfo *ei, GnmValue **args);
static GnmValue* call_perl_function_nodes (FunctionEvalInfo *ei, GnmExprList *expr_tree_list);
static void gplp_finalize (GObject *obj);

static void
gplp_init (GnmPluginLoaderPerl *loader_perl)
{
	g_return_if_fail (IS_GNM_PLUGIN_LOADER_PERL (loader_perl));

	loader_perl->module_name = NULL;
}

static void
gplp_class_init (GObjectClass *gobject_class)
{
	GnmPluginLoaderClass *loader_class = GNM_PLUGIN_LOADER_CLASS (gobject_class);

	parent_class = g_type_class_peek_parent (gobject_class);

	gobject_class->finalize = gplp_finalize;

	loader_class->set_attributes = gplp_set_attributes;
	loader_class->load_base = gplp_load_base;
	loader_class->load_service_function_group = gplp_load_service_function_group;
}

static void
gplp_finalize (GObject *obj)
{
	GnmPluginLoaderPerl *loader_perl = GNM_PLUGIN_LOADER_PERL (obj);

	g_free(loader_perl->module_name);
	loader_perl->module_name = NULL;

	parent_class->finalize (obj);
}

PLUGIN_CLASS (GnmPluginLoaderPerl, gnm_plugin_loader_perl,
	      gplp_class_init, gplp_init,
	      TYPE_GNM_PLUGIN_LOADER)

static void
gplp_set_attributes (GOPluginLoader *loader, GHashTable *attrs, ErrorInfo **ret_error)
{
	GnmPluginLoaderPerl *loader_perl = GNM_PLUGIN_LOADER_PERL (loader);

	gchar *module_name = NULL;

	GO_INIT_RET_ERROR_INFO (ret_error);
	module_name = g_hash_table_lookup (attrs, "module_name");
	if (module_name) {
		loader_perl->module_name = g_strdup (module_name);
	} else {
		*ret_error = error_info_new_str (
			     _("Module name not given."));
	}
}

static void
gplp_load_base (GOPluginLoader *loader, ErrorInfo **ret_error)
{
	//GnmPluginLoaderPerl *loader_perl = GNM_PLUGIN_LOADER_PERL (loader);

	char *argv[] = { (char*)"", NULL, NULL, NULL };
	const char *arg;

	arg = go_plugin_get_dir_name (loader->plugin);
	argv[1] = g_strconcat ("-I", arg, NULL);
	argv[2] = g_build_filename (arg, "perl_func.pl", NULL);

	if (g_file_test (argv[2], G_FILE_TEST_EXISTS)) {
		gnm_perl_interp = perl_alloc ();
		perl_construct (gnm_perl_interp);
		perl_parse (gnm_perl_interp, xs_init, 3, argv, NULL);
		my_perl = gnm_perl_interp;
#ifdef PERL_EXIT_DESTRUCT_END
		PL_exit_flags |= PERL_EXIT_DESTRUCT_END;
#endif
	} else {
		*ret_error = error_info_new_printf (
			     _("perl_func.pl doesn't exist."));
	}

	g_free (argv[1]);
	g_free (argv[2]);
}

static void
gplp_load_service_function_group (GOPluginLoader *loader,
				  GOPluginService *service,
				  ErrorInfo **ret_error)
{
	PluginServiceFunctionGroupCallbacks *cbs;

	g_return_if_fail (IS_GNM_PLUGIN_SERVICE_FUNCTION_GROUP (service));

	GO_INIT_RET_ERROR_INFO (ret_error);

	cbs = plugin_service_get_cbs (service);
	cbs->func_desc_load = &gplp_func_desc_load;
}

static gboolean
gplp_func_desc_load (GOPluginService *service,
		     char const *name,
		     GnmFuncDescriptor *res)
{
	char *args[] = { NULL };
	gchar *help_perl_func = g_strconcat ("help_", name, NULL);
	gchar *desc_perl_func = g_strconcat ("desc_", name, NULL);
	gchar *help_text = NULL;
	gchar *arg_spec = NULL;
	gchar *arg_names = NULL;

	dSP;
	ENTER;
	SAVETMPS;
	PUSHMARK(SP);
	PUTBACK;
	call_argv (help_perl_func, G_EVAL | G_SCALAR | G_NOARGS, args);
	SPAGAIN;

	if (SvTRUE(ERRSV)) { /* Error handling */
		STRLEN n_a;
		g_print ( _("Perl error: %s\n"), SvPV (ERRSV, n_a));
		POPs;
	} else {
        	help_text = g_strdup (POPp);
	}

	PUTBACK;
	FREETMPS;
	LEAVE;

        ENTER;
        SAVETMPS;
        PUSHMARK(SP);
        PUTBACK;
        call_argv (desc_perl_func, G_EVAL | G_ARRAY | G_NOARGS, args);
        SPAGAIN;
                               
	if (SvTRUE(ERRSV)) { /* Error handling */
		STRLEN n_a;
		g_print ( _("Perl error: %s\n"), SvPV (ERRSV, n_a));
		POPs;
	} else {
		arg_names = g_strdup (POPp);
		arg_spec = g_strdup (POPp);
	}

        PUTBACK;
        FREETMPS;
        LEAVE;

	g_free (help_perl_func);
	g_free (desc_perl_func);

	res->name = g_strdup(name);
	res->arg_spec = arg_spec;
	res->arg_names = arg_names;
	res->help = (const char**)&help_text;
	res->fn_args = NULL;
	res->fn_args = &call_perl_function_args;
	res->fn_nodes = NULL;
	res->linker = NULL;
	res->unlinker = NULL;
	res->impl_status = GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC;
	res->test_status = GNM_FUNC_TEST_STATUS_UNKNOWN;

	return TRUE;
}

static GnmValue*
call_perl_function_args (FunctionEvalInfo *ei, GnmValue **args)
{
	GnmFunc const *fndef;
	gint min_n_args, max_n_args, n_args;
	gint i;
	gchar *perl_func = NULL;
	GnmValue* result = NULL;
	dSP;

	fndef = ei->func_call->func;
	perl_func = g_strconcat ("func_", fndef->name, NULL);

	function_def_count_args (fndef, &min_n_args, &max_n_args);
	for (n_args = min_n_args; n_args < max_n_args && args[n_args] != NULL; n_args++);

	ENTER;
	SAVETMPS;
	PUSHMARK(SP);
	for (i=0;i<n_args;i++) {
		g_assert(args[i]->type == VALUE_INTEGER);
		XPUSHs(sv_2mortal(newSViv(args[i]->v_int.val)));
	}
	PUTBACK;
	call_pv (perl_func, G_EVAL | G_SCALAR);
	SPAGAIN;

	if (SvTRUE(ERRSV)) { /* Error handling */
		gchar *errmsg;
		STRLEN n_a;
		errmsg = g_strconcat (_("Perl error: "), SvPV (ERRSV, n_a), NULL);
		POPs;

		result = value_new_error (ei->pos, errmsg);
		g_free (errmsg);
	} else {
		result = perl2value (POPs);
	}

	PUTBACK;
	FREETMPS;
	LEAVE;

	g_free (perl_func);

	return result;
}

static GnmValue*
call_perl_function_nodes (FunctionEvalInfo *ei, GnmExprList *expr_tree_list)
{
	return value_new_int(0);
}

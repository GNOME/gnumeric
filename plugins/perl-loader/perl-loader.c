#include <EXTERN.h>
#include <perl.h>
#include "perl-gnumeric.h"
#undef _
#define _perl_dirty dirty
#undef dirty

#include <gnumeric-config.h>
#include "perl-loader.h"
#include <gnumeric.h>

#include <application.h>
#include <workbook-view.h>
#include <workbook.h>
#include <sheet.h>
#include <value.h>
#include <expr.h>
#include <expr-impl.h>
#include <gnm-plugin.h>

#include <goffice/app/error-info.h>
#include <goffice/app/go-plugin.h>
#include <goffice/app/go-plugin-loader.h>
#include <goffice/app/go-plugin-service.h>
#include <goffice/app/io-context.h>
#include <goffice/app/module-plugin-defs.h>
#include <gsf/gsf-impl-utils.h>

#include <glib/gi18n-lib.h>
#include <stdlib.h>

#define TYPE_GNM_PERL_PLUGIN_LOADER	(gnm_perl_plugin_loader_get_type ())
#define GNM_PERL_PLUGIN_LOADER(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), TYPE_GNM_PERL_PLUGIN_LOADER, GnmPerlPluginLoader))
#define IS_GNM_PERL_PLUGIN_LOADER(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), TYPE_GNM_PERL_PLUGIN_LOADER))

#define dirty _perl_dirty
#undef _perl_dirty

extern void xs_init(pTHX);

static PerlInterpreter* gnm_perl_interp;
static PerlInterpreter* my_perl;

typedef struct {
	GObject	base;
	gchar* module_name;
} GnmPerlPluginLoader;
typedef GObjectClass GnmPerlPluginLoaderClass;

static GnmValue*
call_perl_function_args (GnmFuncEvalInfo *ei, GnmValue const * const *args)
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
		gnm_float f = value_get_as_float (args[i]);
		int i = (int)i;  /* FIXME: someone needs to figure out what this is.  */
		XPUSHs(sv_2mortal(newSViv(i)));
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
#warning FIXME adapt for the new GnmFuncHelp struct.
#if 0
	res->help = (const char**)&help_text;
#endif
	res->fn_args = NULL;
	res->fn_args = &call_perl_function_args;
	res->fn_nodes = NULL;
	res->linker = NULL;
	res->unlinker = NULL;
	res->impl_status = GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC;
	res->test_status = GNM_FUNC_TEST_STATUS_UNKNOWN;

	return TRUE;
}

static void
gplp_set_attributes (GOPluginLoader *loader, GHashTable *attrs, ErrorInfo **ret_error)
{
	GnmPerlPluginLoader *loader_perl = GNM_PERL_PLUGIN_LOADER (loader);

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
	char *argv[] = { (char*)"", NULL, NULL, NULL };
	char const *arg;
	int argc;

	arg = go_plugin_get_dir_name (go_plugin_loader_get_plugin (loader));
	argv[1] = g_strconcat ("-I", arg, NULL);
	argv[2] = g_build_filename (arg, "perl_func.pl", NULL);
	argc = 2;

	if (g_file_test (argv[2], G_FILE_TEST_EXISTS)) {
		PERL_SYS_INIT3(&argc, &argv, NULL);
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
gplp_service_load (GOPluginLoader *l, GOPluginService *s, ErrorInfo **err)
{
	if (IS_GNM_PLUGIN_SERVICE_FUNCTION_GROUP (s))
		gplp_load_service_function_group (l, s, err);
	else
		return FALSE;
	return TRUE;
}

static gboolean
gplp_service_unload (GOPluginLoader *l, GOPluginService *s, ErrorInfo **err)
{
	if (IS_GNM_PLUGIN_SERVICE_FUNCTION_GROUP (s))
		;
	else
		return FALSE;
	return TRUE;
}

static void
gplp_finalize (GObject *obj)
{
	GnmPerlPluginLoader *loader_perl = GNM_PERL_PLUGIN_LOADER (obj);

	g_free (loader_perl->module_name);
	loader_perl->module_name = NULL;

	G_OBJECT_CLASS (g_type_class_peek (G_TYPE_OBJECT))->finalize (obj);
}

static void
go_plugin_loader_init (GOPluginLoaderClass *iface)
{
	iface->set_attributes		= gplp_set_attributes;
	iface->load_base		= gplp_load_base;

	iface->service_load		= gplp_service_load;
	iface->service_unload		= gplp_service_unload;
}

static void
gplp_class_init (GObjectClass *gobject_class)
{
	gobject_class->finalize = gplp_finalize;
}

static void
gplp_init (GnmPerlPluginLoader *loader_perl)
{
	g_return_if_fail (IS_GNM_PERL_PLUGIN_LOADER (loader_perl));

	loader_perl->module_name = NULL;
}

GSF_DYNAMIC_CLASS_FULL (GnmPerlPluginLoader, gnm_perl_plugin_loader,
	NULL, NULL, gplp_class_init, NULL,
	gplp_init, G_TYPE_OBJECT, 0,
	GSF_INTERFACE_FULL (gnm_perl_plugin_loader_type, go_plugin_loader_init, GO_PLUGIN_LOADER_TYPE))

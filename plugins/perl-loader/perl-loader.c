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
#include <gnm-plugin.h>

#include <goffice/goffice.h>
#include <goffice/app/module-plugin-defs.h>
#include <gsf/gsf-impl-utils.h>

#include <glib/gi18n-lib.h>
#include <stdlib.h>

#define TYPE_GNM_PERL_PLUGIN_LOADER	(gnm_perl_plugin_loader_get_type ())
#define GNM_PERL_PLUGIN_LOADER(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), TYPE_GNM_PERL_PLUGIN_LOADER, GnmPerlPluginLoader))
#define GNM_IS_PERL_PLUGIN_LOADER(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), TYPE_GNM_PERL_PLUGIN_LOADER))

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
	GnmFunc *fndef;
	gint min_n_args, max_n_args, n_args;
	gint i;
	gchar *perl_func;
	GnmValue* result;
	dSP;

	fndef = gnm_expr_get_func_def ((GnmExpr *)(ei->func_call));
	perl_func = g_strconcat ("func_", gnm_func_get_name (fndef, FALSE), NULL);

	gnm_func_count_args (fndef, &min_n_args, &max_n_args);
	for (n_args = min_n_args; n_args < max_n_args && args[n_args] != NULL; n_args++);

	ENTER;
	SAVETMPS;
	PUSHMARK(SP);
	for (i = 0; i < n_args; i++) {
		SV* sv = value2perl (args[i]);
		XPUSHs(sv_2mortal(sv));
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

static void
init_help_consts (void)
{
	/* Export our constants as global variables.  */
	const struct {
		const char *name;
		int value;
	} consts[] = {
		{ "GNM_FUNC_HELP_NAME", GNM_FUNC_HELP_NAME },
		{ "GNM_FUNC_HELP_ARG", GNM_FUNC_HELP_ARG },
		{ "GNM_FUNC_HELP_DESCRIPTION", GNM_FUNC_HELP_DESCRIPTION },
		{ "GNM_FUNC_HELP_NOTE", GNM_FUNC_HELP_NOTE },
		{ "GNM_FUNC_HELP_EXAMPLES", GNM_FUNC_HELP_EXAMPLES },
		{ "GNM_FUNC_HELP_SEEALSO", GNM_FUNC_HELP_SEEALSO },
		{ "GNM_FUNC_HELP_EXTREF", GNM_FUNC_HELP_EXTREF },
		{ "GNM_FUNC_HELP_EXCEL", GNM_FUNC_HELP_EXCEL },
		{ "GNM_FUNC_HELP_ODF", GNM_FUNC_HELP_ODF }
	};
	unsigned ui;

	for (ui = 0; ui < G_N_ELEMENTS (consts); ui++) {
		SV* x = get_sv (consts[ui].name, TRUE);
		sv_setiv (x, consts[ui].value);
	}
}

static const char help_template_text[] =
  "This Perl function hasn't been documented.";

static const GnmFuncHelp help_template[] = {
	{ GNM_FUNC_HELP_NAME, NULL },
	{ GNM_FUNC_HELP_DESCRIPTION, NULL },
	{ GNM_FUNC_HELP_END }
};

static GnmFuncHelp *
default_gnm_help(const char *name)
{
	GnmFuncHelp *help = g_new0 (GnmFuncHelp, 3);
	if (help) {
		int i;
		for (i = 0; i < 3; i++)
			help[i] = help_template[i];
		help[0].text = g_strdup_printf ("%s:", name);
		help[1].text = g_strdup (help_template_text);
	}
	return help;
}

static GnmFuncHelp *
make_gnm_help (const char *name, int count, SV **SP)
{
	GnmFuncHelp *help = NULL;
	/* We assume that the description is a Perl array of the form
	   (key, text, key, text, ...). */
	int n = count / 2, m = 0, k, type = GNM_FUNC_HELP_END;
	GnmFuncHelp *helptmp = g_new0 (GnmFuncHelp, n + 1);
	if (count % 2) POPs, count--;
	for (k = n; k-- > 0; ) {
		SV *sv = POPs;
		if (SvPOK(sv)) {
			STRLEN size;
			gchar *tmp;
			tmp = SvPV(sv, size);
			helptmp[k].text = g_strndup (tmp, size);
		} else {
			helptmp[k].text = NULL;
		}
		sv = POPs;
		if (SvIOK(sv)) type = SvIV(sv);
		if (helptmp[k].text &&
		    type >= GNM_FUNC_HELP_NAME && GNM_FUNC_HELP_ODF) {
			helptmp[k].type = type; m++;
		} else {
			helptmp[k].type = GNM_FUNC_HELP_END;
			if (helptmp[k].text)
				g_free ((char*)helptmp[k].text);
			helptmp[k].text = NULL;
		}
	}
	if (m == 0) {
		/* No valid entries. */
		g_free (helptmp);
	} else {
		/* Collect all valid entries in a new array. */
		if (n == m) {
			help = helptmp;
		} else {
			int i;
			help = g_new (GnmFuncHelp, m+1);
			for (i = 0, k = 0; k < n; k++)
				if (helptmp[k].type != GNM_FUNC_HELP_END &&
				    helptmp[k].text)
					help[i++] = helptmp[k];
			g_free(helptmp);
		}
		help[m].type = GNM_FUNC_HELP_END;
		help[m].text = NULL;
	}
	if (!help) /* Provide a reasonable default. */
		help = default_gnm_help (name);

	gnm_perl_loader_free_later (help);
	for (n = 0; help[n].type != GNM_FUNC_HELP_END; n++)
		gnm_perl_loader_free_later (help[n].text);

	return help;
}

static void
gplp_func_load_stub (GOPluginService *service, GnmFunc *func)
{
	char const *name = gnm_func_get_name (func, FALSE);
	char *args[] = { NULL };
	gchar *help_perl_func = g_strconcat ("help_", name, NULL);
	gchar *desc_perl_func = g_strconcat ("desc_", name, NULL);
	GnmFuncHelp *help = NULL;
	gchar *arg_spec = NULL;
	int count;

	dSP;
	ENTER;
	SAVETMPS;
	PUSHMARK(SP);
	PUTBACK;
	count = call_argv (help_perl_func, G_EVAL | G_ARRAY | G_NOARGS, args);
	SPAGAIN;

	if (SvTRUE(ERRSV)) { /* Error handling */
		STRLEN n_a;
		g_print ( _("Perl error: %s\n"), SvPV (ERRSV, n_a));
		while (count-- > 0) POPs;
	} else {
		help = make_gnm_help(name, count, SP);
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
		arg_spec = g_strdup (POPp);
		gnm_perl_loader_free_later (arg_spec);
	}

        PUTBACK;
        FREETMPS;
        LEAVE;

	g_free (help_perl_func);
	g_free (desc_perl_func);

	gnm_func_set_fixargs (func, call_perl_function_args, arg_spec);
	gnm_func_set_help (func, help, -1);
	gnm_func_set_impl_status (func, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC);
}

static void
gplp_set_attributes (GOPluginLoader *loader, GHashTable *attrs, GOErrorInfo **ret_error)
{
	GnmPerlPluginLoader *loader_perl = GNM_PERL_PLUGIN_LOADER (loader);

	gchar *module_name = NULL;

	GO_INIT_RET_ERROR_INFO (ret_error);
	module_name = g_hash_table_lookup (attrs, "module_name");
	if (module_name) {
		loader_perl->module_name = g_strdup (module_name);
	} else {
		*ret_error = go_error_info_new_str (
			     _("Module name not given."));
	}
}

static void
gplp_load_base (GOPluginLoader *loader, GOErrorInfo **ret_error)
{
	char *argv[] = { (char*)"", NULL, NULL, NULL };
	char const *arg;
	int argc;

	arg = go_plugin_get_dir_name (go_plugin_loader_get_plugin (loader));
	argv[1] = g_strconcat ("-I", arg, NULL);
	argv[2] = g_build_filename (arg, "perl_func.pl", NULL);
	argc = 2;

	if (g_file_test (argv[2], G_FILE_TEST_EXISTS)) {
		PERL_SYS_INIT3 (&argc, (char ***)&argv, NULL);
		gnm_perl_interp = perl_alloc ();
		perl_construct (gnm_perl_interp);
		perl_parse (gnm_perl_interp, xs_init, 3, argv, NULL);
		my_perl = gnm_perl_interp;
		init_help_consts ();
#ifdef PERL_EXIT_DESTRUCT_END
		PL_exit_flags |= PERL_EXIT_DESTRUCT_END;
#endif
	} else {
		*ret_error = go_error_info_new_printf (
			     _("perl_func.pl doesn't exist."));
	}

	g_free (argv[1]);
	g_free (argv[2]);
}

static void
gplp_load_service_function_group (GOPluginLoader *loader,
				  GOPluginService *service,
				  GOErrorInfo **ret_error)
{
	GnmPluginServiceFunctionGroupCallbacks *cbs;

	g_return_if_fail (GNM_IS_PLUGIN_SERVICE_FUNCTION_GROUP (service));

	GO_INIT_RET_ERROR_INFO (ret_error);

	cbs = go_plugin_service_get_cbs (service);
	cbs->load_stub = &gplp_func_load_stub;
}

static gboolean
gplp_service_load (GOPluginLoader *l, GOPluginService *s, GOErrorInfo **err)
{
	if (GNM_IS_PLUGIN_SERVICE_FUNCTION_GROUP (s))
		gplp_load_service_function_group (l, s, err);
	else
		return FALSE;
	return TRUE;
}

static gboolean
gplp_service_unload (GOPluginLoader *l, GOPluginService *s, GOErrorInfo **err)
{
	if (GNM_IS_PLUGIN_SERVICE_FUNCTION_GROUP (s))
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
	g_return_if_fail (GNM_IS_PERL_PLUGIN_LOADER (loader_perl));

	loader_perl->module_name = NULL;
}

GSF_DYNAMIC_CLASS_FULL (GnmPerlPluginLoader, gnm_perl_plugin_loader,
	NULL, NULL, gplp_class_init, NULL,
	gplp_init, G_TYPE_OBJECT, 0,
	GSF_INTERFACE_FULL (gnm_perl_plugin_loader_type, go_plugin_loader_init, GO_TYPE_PLUGIN_LOADER))

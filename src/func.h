#ifndef _GNM_FUNC_H_
# define _GNM_FUNC_H_

#include <gnumeric.h>
#include <dependent.h>

G_BEGIN_DECLS

/* Setup of the symbol table */
void gnm_func_init_     (void);
void gnm_func_shutdown_ (void);

GPtrArray *gnm_func_enumerate (void);

/******************************************************************************/
/* Function group support */

struct _GnmFuncGroup {
	GOString *internal_name, *display_name;
	gboolean has_translation;
	GSList *functions;
	unsigned ref_count; /* boxed type */
};

GType gnm_func_group_get_type (void); /* boxed type */
GnmFuncGroup *gnm_func_group_get_nth (gint n);
GnmFuncGroup *gnm_func_group_fetch (char const *name,
				    char const *translation);

/******************************************************************************/

/*
 * Function registration routines
 *
 * Functions come in two fashions:  Those that only deal with
 * very specific data types and a constant number of arguments,
 * and those who don't.
 *
 * The former kind of functions receives a precomputed array of
 * GnmValue pointers.
 *
 * The latter sort of functions receives the plain ExprNodes and
 * it is up to that routine to do the value computations and range
 * processing.
 */

/*
 *  Argument tokens passed in 'args'
 *
 * With intersection and iteration support
 *	f : floating point	(no errors, string conversion attempted)
 *	b : boolean		(identical to f, Do we need this ?)
 *	s : string		(no errors)
 *	S : 'scalar': any non-error value
 *	E : scalar including errors
 * Without intersection or iteration support
 *	r : cell range	content is _NOT_ guaranteed to have been evaluated yet
 *	A : area	either range or array (as above)
 *	? : anything
 *
 *  For optional arguments do:
 * "ff|ss" where the strings are optional
 */

typedef enum {
	GNM_FUNC_TYPE_ARGS,	/* Arguments get marshalled by type */
	GNM_FUNC_TYPE_NODES,	/* Takes unevaulated expers directly */

	/* implementation has not been loaded yet, but we know where it is */
	GNM_FUNC_TYPE_STUB
} GnmFuncType;

typedef enum {
	GNM_FUNC_SIMPLE			= 0,
	GNM_FUNC_VOLATILE		= 1 << 0, /* eg now(), today() */
	GNM_FUNC_RETURNS_NON_SCALAR	= 1 << 1, /* eg transpose(), mmult() */

	/* an unknown function that will hopefully be defined later */
	GNM_FUNC_IS_PLACEHOLDER		= 1 << 3,
	GNM_FUNC_IS_WORKBOOK_LOCAL	= 1 << 5,
	GNM_FUNC_INTERNAL		= 1 << 6,

	GNM_FUNC_AUTO_UNKNOWN           = 0 << 8,
	GNM_FUNC_AUTO_MONETARY          = 1 << 8,  /* Like PV */
	GNM_FUNC_AUTO_DATE              = 2 << 8,  /* Like DATE */
	GNM_FUNC_AUTO_TIME              = 3 << 8,  /* Like TIME */
	GNM_FUNC_AUTO_PERCENT           = 4 << 8,  /* Like IRR */
	GNM_FUNC_AUTO_FIRST             = 5 << 8,  /* Like SUM */
	GNM_FUNC_AUTO_SECOND            = 6 << 8,  /* Like IF */
	GNM_FUNC_AUTO_UNITLESS          = 7 << 8,  /* Like COUNT */
	GNM_FUNC_AUTO_MASK              = 7 << 8   /* The bits we use for AUTO.  */
} GnmFuncFlags;

/* I do not like this.  It is going to be different for different apps probably
 * want to split it into bit file with our notion of its state, and 2 bits of
 * state per import format.
 */
typedef enum {
	GNM_FUNC_IMPL_STATUS_EXISTS = 0,
	GNM_FUNC_IMPL_STATUS_UNIMPLEMENTED,
	GNM_FUNC_IMPL_STATUS_SUBSET,
	GNM_FUNC_IMPL_STATUS_COMPLETE,
	GNM_FUNC_IMPL_STATUS_SUPERSET,
	GNM_FUNC_IMPL_STATUS_SUBSET_WITH_EXTENSIONS,
	GNM_FUNC_IMPL_STATUS_UNDER_DEVELOPMENT,
	GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC
} GnmFuncImplStatus;

typedef enum {
	GNM_FUNC_TEST_STATUS_UNKNOWN = 0,
	GNM_FUNC_TEST_STATUS_NO_TESTSUITE,
	GNM_FUNC_TEST_STATUS_BASIC,
	GNM_FUNC_TEST_STATUS_EXHAUSTIVE,
	GNM_FUNC_TEST_STATUS_UNDER_DEVELOPMENT
} GnmFuncTestStatus;

typedef GnmValue	*(*GnmFuncArgs)	  (GnmFuncEvalInfo *ei, GnmValue const * const *args);
typedef GnmValue	*(*GnmFuncNodes)  (GnmFuncEvalInfo *ei,
					   int argc, GnmExprConstPtr const *argv);

typedef enum {
	GNM_FUNC_HELP_END,		/* Format */
					/* ------ */
	GNM_FUNC_HELP_NAME,
	/* <NAME>:<1 SENTENCE DESCRIPTION> (translated) */

	GNM_FUNC_HELP_ARG,
	/* <NAME>:<1 SENTENCE DESCRIPTION> (translated) */

	GNM_FUNC_HELP_DESCRIPTION,
	/* <LONG DESCRIPTION (reference args using @{arg})> (translated) */

	GNM_FUNC_HELP_NOTE,
	/* <SPECIAL CASES (reference args using @{arg})> (translated) */

	GNM_FUNC_HELP_EXAMPLES,
	/*
	 * Either translated text, or a formula that is only marked for
	 * translation if it contains strings that need to be translated.
	 */

	GNM_FUNC_HELP_SEEALSO,
	/* name,name,name ...	(not translated) */

	GNM_FUNC_HELP_EXTREF,
	/*
	 * Link to external descriptions.  The following styles defined:
	 * wolfram:Sine.html
	 * wiki:en:Trigonometric_functions
	 */

	GNM_FUNC_HELP_EXCEL,
	/* <SPECIAL NOTE RE EXCEL (reference args using @{arg})> (translated) */

	GNM_FUNC_HELP_ODF
	/* <SPECIAL NOTE RE ODF (reference args using @{arg})> (translated) */
} GnmFuncHelpType;

typedef struct {
    GnmFuncHelpType	 type;
    char const		*text;
} GnmFuncHelp;

struct _GnmFuncDescriptor {
	char const *name;
	char const *arg_spec;
	GnmFuncHelp const *help;
	GnmFuncArgs	  fn_args;
	GnmFuncNodes	  fn_nodes;
	GnmFuncFlags	  flags;
	GnmFuncImplStatus impl_status;
	GnmFuncTestStatus test_status;
};

struct GnmFunc_ {
	GObject	base;

	char const *name;
	GnmFuncHelp *help;

	/* <private> */
	GnmFuncType fn_type;
	GnmFuncGroup *fn_group;
	GnmFuncFlags flags;
	GnmFuncImplStatus impl_status;
	GnmFuncTestStatus test_status;

	GOString *tdomain;
	char *localized_name;

	gint usage_count;

	// Meaningful for ARGS only
	char *arg_spec;
	GnmFuncArgs args_func;

	// Meaningful for NODES only
	GnmFuncNodes nodes_func;

	// Derived for quick access
	GPtrArray *arg_names;
	int min_args, max_args;
	char *arg_types;
	int help_count;
};

#define GNM_FUNC_TYPE	(gnm_func_get_type ())
#define GNM_FUNC(obj)   (G_TYPE_CHECK_INSTANCE_CAST ((obj), GNM_FUNC_TYPE, GnmFunc))
#define GNM_IS_FUNC(o)  (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_FUNC_TYPE))

GType       gnm_func_get_type        (void);
void        gnm_func_load_if_stub    (GnmFunc *func);
void        gnm_func_dispose         (GnmFunc *func);

GnmFunc	   *gnm_func_inc_usage	     (GnmFunc *func);
void	    gnm_func_dec_usage	     (GnmFunc *func);
gboolean    gnm_func_get_in_use      (GnmFunc *func);

char const *gnm_func_get_translation_domain (GnmFunc const *func);
void        gnm_func_set_translation_domain (GnmFunc *func,
					     const char *tdomain);
char const *gnm_func_gettext         (GnmFunc const *func, const char *str);

GnmFuncFlags gnm_func_get_flags      (GnmFunc const *func);
void        gnm_func_set_flags       (GnmFunc *func, GnmFuncFlags f);


GnmFuncImplStatus gnm_func_get_impl_status (GnmFunc const *func);
void        gnm_func_set_impl_status (GnmFunc *func, GnmFuncImplStatus st);

GnmFuncTestStatus gnm_func_get_test_status (GnmFunc const *func);
void        gnm_func_set_test_status (GnmFunc *func, GnmFuncTestStatus st);

GnmFuncGroup*gnm_func_get_function_group (GnmFunc *func);
void        gnm_func_set_function_group (GnmFunc *func, GnmFuncGroup *group);

gboolean    gnm_func_is_varargs      (GnmFunc const *func);
gboolean    gnm_func_is_fixargs      (GnmFunc const *func);

void        gnm_func_set_stub        (GnmFunc *func);
void        gnm_func_set_varargs     (GnmFunc *func, GnmFuncNodes fn,
				      const char *spec);
void        gnm_func_set_fixargs     (GnmFunc *func, GnmFuncArgs fn,
				      const char *spec);

GnmFuncHelp const *gnm_func_get_help (GnmFunc const *func, int *n);
void        gnm_func_set_help (GnmFunc *func, GnmFuncHelp const *help, int n);

GnmDependentFlags gnm_func_link_dep (GnmFunc *func, GnmFuncEvalInfo *ei, gboolean qlink);

char const *gnm_func_get_name	     (GnmFunc const *func,
				      gboolean localized);
GnmFunc	   *gnm_func_lookup	     (char const *name, Workbook *scope);
GnmFunc    *gnm_func_lookup_localized (char const *name, Workbook *scope);
GSList	   *gnm_func_lookup_prefix   (char const *prefix, Workbook *scope,
				      gboolean trans);
GnmFunc    *gnm_func_add	     (GnmFuncGroup *group,
				      GnmFuncDescriptor const *descriptor,
				      const char *tdomain);
void        gnm_func_set_from_desc   (GnmFunc *func, GnmFuncDescriptor const *desc);

GnmFunc    *gnm_func_add_placeholder (Workbook *scope,
				      char const *name,
				      char const *type);
GnmFunc    *gnm_func_add_placeholder_localized (char const *gname, char const *lname);
GnmFunc	   *gnm_func_lookup_or_add_placeholder (char const *name);

/* TODO */
char const *gnm_func_get_description (GnmFunc const *func);
void        gnm_func_count_args    (GnmFunc const *func, gint *min, int *max);
char        gnm_func_get_arg_type  (GnmFunc const *func, gint arg_idx);
char const *gnm_func_get_arg_type_string  (GnmFunc const *func, gint arg_idx);
char       *gnm_func_get_arg_name  (GnmFunc const *func, guint arg_idx);
char const *gnm_func_get_arg_description (GnmFunc const *func, guint arg_idx);
char       *gnm_func_convert_markup_to_pango (char const *desc,
					      GtkWidget *target);

/*************************************************************************/

GnmValue *function_call_with_exprs	(GnmFuncEvalInfo *ei);
GnmValue *function_call_with_values     (GnmEvalPos const *ep, char const *name,
					 int argc, GnmValue const * const *values);
GnmValue *function_def_call_with_values (GnmEvalPos const *ep, GnmFunc const *fn,
					 int argc, GnmValue const * const *values);

/* Utilities to interate through ranges and argument lists */
typedef GnmValue * (*FunctionIterateCB) (GnmEvalPos const *ep, GnmValue const *value,
					 gpointer user_data);
GnmValue *function_iterate_argument_values (GnmEvalPos const *ep,
					    FunctionIterateCB callback,
					    gpointer callback_closure,
					    int argc,
					    GnmExprConstPtr const *argv,
					    gboolean strict,
					    CellIterFlags iter_flags);

/*************************************************************************/

struct _GnmFuncEvalInfo {
	GnmEvalPos const *pos;
	GnmExprFunction const *func_call;
	GnmExprEvalFlags flags;
};

GnmFunc const *gnm_eval_info_get_func (GnmFuncEvalInfo const *ei);
int gnm_eval_info_get_arg_count (GnmFuncEvalInfo const *ei);

/*************************************************************************/

GnmExpr const *gnm_func_derivative (GnmFunc *func,
				    GnmExpr const *expr, GnmEvalPos const *ep,
				    GnmExprDeriv *info);


G_END_DECLS

#endif /* _GNM_FUNC_H_ */

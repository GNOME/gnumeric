#ifndef GNUMERIC_FUNC_H
#define GNUMERIC_FUNC_H

#include "gnumeric.h"
#include "dependent.h"

/* Setup of the symbol table */
void functions_init     (void);
void functions_shutdown (void);

/* Used to build manual */
void function_dump_defs (char const *filename, gboolean def_or_state);

/******************************************************************************/
/* Function category support */

typedef struct _FunctionCategory FunctionCategory;
struct _FunctionCategory {
	String *internal_name, *display_name;
	gboolean has_translation;
	GList *functions;
};

FunctionCategory *function_get_category     (char const *name);
FunctionCategory *function_get_category_with_translation (char const *name,
                                                          char const *translation);
FunctionCategory *function_category_get_nth (gint n);
void function_category_add_func (FunctionCategory *, GnmFunc *);
void function_category_remove_func (FunctionCategory *category, GnmFunc *fn_def);

/******************************************************************************/

/*
 * Function registration routines
 *
 * Functions come in two fashions:  Those that only deal with
 * very specific data types and a constant number of arguments,
 * and those who don't.
 *
 * The former kind of functions receives a precomputed array of
 * Value pointers.
 *
 * The latter sort of functions receives the plain ExprNodes and
 * it is up to that routine to do the value computations and range
 * processing.
 */

/**
 *  Argument tokens passed in 'args'
 *
 * With intersection and iteration support
 * 	f : float 		(no errors, string conversion attempted)
 * 	b : boolean		(identical to f, Do we need this ?)
 * 	s : string		(no errors)
 * 	S : 'scalar': any non-error value
 * 	E : scalar including errors
 * Without intersection or iteration support
 *	r : cell range	content is _NOT_ guaranteed to have been evaluated yet
 *	A : area	either range or array (as above)
 *	a : array
 *	? : anything
 *
 *  For optional arguments do:
 * "ff|ss" where the strings are optional
 **/

typedef enum {
	GNM_FUNC_TYPE_ARGS,	/* Takes unevaulated expers directly */
	GNM_FUNC_TYPE_NODES,	/* arguments get marshalled by type */

	/* implementation has not been loaded yet, but we know where it is */
	GNM_FUNC_TYPE_STUB
} GnmFuncType;

typedef enum {
	GNM_FUNC_SIMPLE			= 0,
	GNM_FUNC_VOLATILE		= 0x01, /* eg now(), today() */
	GNM_FUNC_RETURNS_NON_SCALAR	= 0x02, /* eg transpose(), mmult() */

	/* For functions that are not exactly compatible with various import
	 * formats.  We need to recalc their results to avoid changing values
	 * unexpectedly when we recalc later.  This probably needs to be done
	 * on a per import format basis.  It may not belong here.
	 */
	GNM_FUNC_RECALC_ONLOAD 		= 0x04,

	/* an unknown function that will hopefully be defined later */
	GNM_FUNC_IS_PLACEHOLDER		= 0x08
} GnmFuncFlags;

/* I do not like this it is going to be different for different apps
 * probably want to split it into bit file with our notion of its state, and 2
 * bits of state per import format.
 */
typedef enum {
	GNM_FUNC_IMPL_STATUS_EXISTS = 0,
	GNM_FUNC_IMPL_STATUS_UNIMPLEMENTED,
	GNM_FUNC_IMPL_STATUS_SUBSET,
	GNM_FUNC_IMPL_STATUS_COMPLETE,
	GNM_FUNC_IMPL_STATUS_SUPERSET,
	GNM_FUNC_IMPL_STATUS_SUBSET_WITH_EXTENSIONS,
	GNM_FUNC_IMPL_STATUS_UNDER_DEVELOPMENT,
	GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
} GnmFuncImplStatus;

typedef enum {
	GNM_FUNC_TEST_STATUS_UNKNOWN = 0,
	GNM_FUNC_TEST_STATUS_NO_TESTSUITE,
	GNM_FUNC_TEST_STATUS_BASIC,
	GNM_FUNC_TEST_STATUS_EXHAUSTIVE,
	GNM_FUNC_TEST_STATUS_UNDER_DEVELOPMENT
} GnmFuncTestStatus;
typedef struct _GnmFuncDescriptor GnmFuncDescriptor;

typedef Value 		*(*GnmFuncArgs)	  (FunctionEvalInfo *ei, Value **args);
typedef Value 		*(*GnmFuncNodes)  (FunctionEvalInfo *ei, GnmExprList *l);
typedef DependentFlags	 (*GnmFuncLink)	  (FunctionEvalInfo *ei);
typedef void		 (*GnmFuncUnlink) (FunctionEvalInfo *ei);

typedef void	 (*GnmFuncRefNotify) (GnmFunc *f, int refcount);
typedef gboolean (*GnmFuncLoadDesc)  (GnmFunc const *f, GnmFuncDescriptor *fd);

struct _GnmFuncDescriptor {
	char const *name;
	char const *arg_spec;
	char const *arg_names;
	char const **help;	/* this is easier for compilers */
	GnmFuncArgs	  fn_args;
	GnmFuncNodes	  fn_nodes;
	GnmFuncLink	  linker;
	GnmFuncUnlink	  unlinker;
	GnmFuncRefNotify  ref_notify;
	GnmFuncFlags	  flags;
	GnmFuncImplStatus impl_status;
	GnmFuncTestStatus test_status;
};

struct _GnmFunc {
	char const *name;
	char const *arg_names;
	char const *help;
	GnmFuncType fn_type;
	union {
		GnmFuncNodes nodes;
		struct {
			char const *arg_spec;
			GnmFuncArgs  func;
			int min_args, max_args;
			char *arg_types;
		} args;
		GnmFuncLoadDesc	load_desc;
	} fn;
	GnmFuncLink		linker;
	GnmFuncUnlink		unlinker;
	GnmFuncRefNotify	ref_notify;
	GnmFuncImplStatus	impl_status;
	GnmFuncTestStatus	test_status;
	GnmFuncFlags		flags;

	gint         		ref_count;
	gpointer     		user_data;
};

struct _FunctionEvalInfo {
	EvalPos const *pos;
	GnmExprFunction const *func_call;
};

void	    gnm_func_ref	     (GnmFunc *func);
void	    gnm_func_unref	     (GnmFunc *func);
void	    gnm_func_load_stub	     (GnmFunc *fn_def);
char const *gnm_func_get_name	     (GnmFunc const *fn_def);
gpointer    gnm_func_get_user_data   (GnmFunc const *func);
void        gnm_func_set_user_data   (GnmFunc *func, gpointer user_data);
GnmFunc	   *gnm_func_lookup	     (char const *name, Workbook const *scope);
GnmFunc    *gnm_func_add	     (FunctionCategory *category,
				      GnmFuncDescriptor const *descriptor);
GnmFunc    *gnm_func_add_stub	     (FunctionCategory *category,
				      char const *name,
				      GnmFuncLoadDesc  load_desc,
				      GnmFuncRefNotify opt_ref_notify);
GnmFunc    *gnm_func_add_placeholder (char const *name, char const *type,
				      gboolean copy_name);

/* TODO */
void                function_remove     (FunctionCategory *category,
                                         char const *name);

void        function_def_count_args    (GnmFunc const *fn_def,
                                        gint *min, int *max);
char        function_def_get_arg_type  (GnmFunc const *fn_def,
                                        gint arg_idx);
char const *function_def_get_arg_type_string  (GnmFunc const *fn_def,
                                        gint arg_idx);
char       *function_def_get_arg_name  (GnmFunc const *fn_def,
                                        gint arg_idx);

/*************************************************************************/

Value *function_call_with_list	     (FunctionEvalInfo *ei, GnmExprList *args,
				      GnmExprEvalFlags flags);
Value *function_call_with_values     (EvalPos const *ep, char const *name,
                                      gint argc, Value *values []);
Value *function_def_call_with_values (EvalPos const *ep, GnmFunc const *fn,
                                      gint argc, Value *values []);

/* Utilies to interate through ranges and argument lists */
typedef Value * (*FunctionIterateCB) (EvalPos const *ep,
                                      Value *value, gpointer user_data);
Value *function_iterate_argument_values	(EvalPos const	   *ep,
                                         FunctionIterateCB  cb,
                                         gpointer           user_data,
                                         GnmExprList       *expr_node_list,
                                         gboolean           strict,
                                         CellIterFlags	    iter_flags);
Value *function_iterate_do_value	(EvalPos const      *ep,
					 FunctionIterateCB   cb,
					 gpointer            user_data,
					 Value              *value,
					 gboolean            strict,
					 CellIterFlags	     iter_flags);

/******************************************************************************/

/* Detailed function help */
typedef struct {
	GPtrArray *sections;
	gboolean   help_is_localized;
	char     *help_copy;
	GnmFunc const *fndef;
} TokenizedHelp;

TokenizedHelp *tokenized_help_new     (GnmFunc const *fn_def);
char const    *tokenized_help_find    (TokenizedHelp *tok, char const *token);
void           tokenized_help_destroy (TokenizedHelp *tok);

#endif /* GNUMERIC_FUNC_H */

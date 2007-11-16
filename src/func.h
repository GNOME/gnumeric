/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_FUNC_H_
# define _GNM_FUNC_H_

#include "gnumeric.h"
#include "dependent.h"

G_BEGIN_DECLS

/* Setup of the symbol table */
void functions_init     (void);
void functions_shutdown (void);

void function_dump_defs (char const *filename, int dump_type);

/******************************************************************************/
/* Function group support */

struct _GnmFuncGroup {
	GnmString *internal_name, *display_name;
	gboolean has_translation;
	GSList *functions;
};

GnmFuncGroup *gnm_func_group_get_nth (gint n);
GnmFuncGroup *gnm_func_group_fetch     		    (char const *name);
GnmFuncGroup *gnm_func_group_fetch_with_translation (char const *name,
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
	GNM_FUNC_TYPE_ARGS,	/* Arguments get marshalled by type */
	GNM_FUNC_TYPE_NODES,	/* Takes unevaulated expers directly */

	/* implementation has not been loaded yet, but we know where it is */
	GNM_FUNC_TYPE_STUB
} GnmFuncType;

typedef enum {
	GNM_FUNC_SIMPLE			= 0,
	GNM_FUNC_VOLATILE		= 1 << 0, /* eg now(), today() */
	GNM_FUNC_RETURNS_NON_SCALAR	= 1 << 1, /* eg transpose(), mmult() */

	/* For functions that are not exactly compatible with various import
	 * formats.  We need to recalc their results to avoid changing values
	 * unexpectedly when we recalc later.  This probably needs to be done
	 * on a per import format basis.  It may not belong here.
	 */
	GNM_FUNC_RECALC_ONLOAD 		= 1 << 2,

	/* an unknown function that will hopefully be defined later */
	GNM_FUNC_IS_PLACEHOLDER		= 1 << 3,
	GNM_FUNC_FREE_NAME		= 1 << 4,
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

typedef GnmValue 	*(*GnmFuncArgs)	  (GnmFuncEvalInfo *ei, GnmValue const * const *args);
typedef GnmValue 	*(*GnmFuncNodes)  (GnmFuncEvalInfo *ei,
					   int argc, GnmExprConstPtr const *argv);
typedef DependentFlags	 (*GnmFuncLink)	  (GnmFuncEvalInfo *ei);
typedef void		 (*GnmFuncUnlink) (GnmFuncEvalInfo *ei);

typedef void	 (*GnmFuncRefNotify) (GnmFunc *f, int refcount);
typedef gboolean (*GnmFuncLoadDesc)  (GnmFunc const *f, GnmFuncDescriptor *fd);

typedef enum {
	GNM_FUNC_HELP_END,		/* Format */
					/* ------ */
	GNM_FUNC_HELP_OLD,		/* old token based format */
	GNM_FUNC_HELP_NAME,		/* <NAME>:<1 SENTENCE DESCRIPTION>	(translated) */
	GNM_FUNC_HELP_ARG,		/* <NAME>:<1 SENTENCE DESCRIPTION> 	(translated) */
	GNM_FUNC_HELP_DESCRIPTION,	/* <LONG DESCRIPTION (reference args using @{arg})> 		(translated) */
	GNM_FUNC_HELP_NOTE,		/* <SPECIAL CASES (reference args using @{arg})> 		(translated) */
	GNM_FUNC_HELP_EXAMPLES,		/* <TEXT and EXAMPLES ?? get a hook to enter the sample ?? > 	(translated) */
	GNM_FUNC_HELP_SEEALSO		/* name,name,name ...			(not translated) */
} GnmFuncHelpType;
typedef struct {
    GnmFuncHelpType	 type;
    char const		*text;
} GnmFuncHelp;

struct _GnmFuncDescriptor {
	char const *name;
	char const *arg_spec;
	char const *arg_names;
	GnmFuncHelp const *help;
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
	GnmFuncHelp const *help;
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
	GnmFuncGroup		*fn_group; /* most recent it was assigned to */
	GnmFuncLink		 linker;
	GnmFuncUnlink		 unlinker;
	GnmFuncRefNotify	 ref_notify;
	GnmFuncImplStatus	 impl_status;
	GnmFuncTestStatus	 test_status;
	GnmFuncFlags		 flags;

	gint         		 ref_count;
	gpointer     		 user_data;
};

struct _GnmFuncEvalInfo {
	GnmEvalPos const *pos;
	GnmExprFunction const *func_call;
};

void	    gnm_func_free	     (GnmFunc *func);
void	    gnm_func_ref	     (GnmFunc *func);
void	    gnm_func_unref	     (GnmFunc *func);
void        gnm_func_load_if_stub    (GnmFunc *func);
void	    gnm_func_load_stub	     (GnmFunc *fn_def);
char const *gnm_func_get_name	     (GnmFunc const *fn_def);
gpointer    gnm_func_get_user_data   (GnmFunc const *func);
void        gnm_func_set_user_data   (GnmFunc *func, gpointer user_data);
GnmFunc	   *gnm_func_lookup	     (char const *name, Workbook *scope);	// change scope one day
GnmFunc    *gnm_func_add	     (GnmFuncGroup *group,
				      GnmFuncDescriptor const *descriptor);
GnmFunc    *gnm_func_add_stub	     (GnmFuncGroup *group,
				      char const *name,
				      GnmFuncLoadDesc  load_desc,
				      GnmFuncRefNotify opt_ref_notify);
GnmFunc    *gnm_func_add_placeholder (Workbook *optional_scope,			// change scope one day
				      char const *name,
				      char const *type,
				      gboolean copy_name);

/* TODO */
void        function_def_count_args    (GnmFunc const *fn_def,
                                        gint *min, int *max);
char        function_def_get_arg_type  (GnmFunc const *fn_def,
                                        gint arg_idx);
char const *function_def_get_arg_type_string  (GnmFunc const *fn_def,
                                        gint arg_idx);
char       *function_def_get_arg_name  (GnmFunc const *fn_def,
                                        gint arg_idx);

/*************************************************************************/

GnmValue *function_call_with_exprs	(GnmFuncEvalInfo *ei,
					 GnmExprEvalFlags flags);
GnmValue *function_call_with_values     (GnmEvalPos const *ep, char const *name,
					 int argc, GnmValue const * const *values);
GnmValue *function_def_call_with_values (GnmEvalPos const *ep, GnmFunc const *fn,
					 int argc, GnmValue const * const *values);

/* Utilies to interate through ranges and argument lists */
typedef GnmValue * (*FunctionIterateCB) (GnmEvalPos const *ep, GnmValue const *value,
					 gpointer user_data);
GnmValue *function_iterate_argument_values (GnmEvalPos const *ep,
					    FunctionIterateCB cb,
					    gpointer user_data,
					    int argc,
					    GnmExprConstPtr const *argv,
					    gboolean strict,
					    CellIterFlags iter_flags);
GnmValue *function_iterate_do_value	(GnmEvalPos const   *ep,
					 FunctionIterateCB   cb,
					 gpointer            user_data,
					 GnmValue const     *value,
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

G_END_DECLS

#endif /* _GNM_FUNC_H_ */

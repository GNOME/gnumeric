#ifndef GNUMERIC_FUNC_H
#define GNUMERIC_FUNC_H

#include "gnumeric.h"
#include "eval.h"

/* Setup of the symbol table */
void functions_init     (void);
/* Used to build manual */
void function_dump_defs (char const *filename);

/******************************************************************************/
/* Function category support */

typedef struct _FunctionCategory FunctionCategory;
struct _FunctionCategory {
	String *internal_name, *display_name;
	gboolean has_translation;
	GList *functions;
};

FunctionCategory *function_get_category     (gchar const *name);
FunctionCategory *function_get_category_with_translation (gchar const *name,
                                                          gchar const *translation);
FunctionCategory *function_category_get_nth (gint n);
void function_category_add_func (FunctionCategory *, FunctionDefinition *);
void function_category_remove_func (FunctionCategory *category, FunctionDefinition *fn_def);

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
 **
 *  The types accepted: see writing-functions.smgl ( bottom )
 * f for float
 * s for string
 * b for boolean
 * r for cell range
 * a for cell array
 * A for 'area': either range or array
 * S for 'scalar': anything OTHER than an array or range
 * ? for any kind
 *  For optional arguments do:
 * "ff|ss" where the strings are optional
 **/

/* These are not supported yet */
typedef enum {
	FUNCTION_RETURNS_ARRAY = 0x01, /* eg transpose(), mmult() */
	FUNCTION_RECALC_ALWAYS = 0x02, /* eg now(), today() */

	/* For functions that are not exactly compatible with various import
	 * formats.  We need to recalc their results to avoid changing values
	 * unexpectedly when we recalc later.  This probably needs to be done
	 * on a per import format basis.  It may not belong here.
	 */
	FUNCTION_RECALC_ONLOAD = 0x04

	/* TODO : Are there other forms or recalc we need to think about ? */
} FunctionFlags;

typedef enum { FUNCTION_ARGS, FUNCTION_NODES, FUNCTION_NAMEONLY } FuncType;

typedef Value *(*FunctionArgs)  (FunctionEvalInfo *ei, Value **args);
typedef Value *(*FunctionNodes) (FunctionEvalInfo *ei, GnmExprList *nodes);

struct _FunctionEvalInfo {
	EvalPos const *pos;
	GnmExprFunction const *func_call;
};

typedef DependentFlags	(*FuncLinkHandle) 	(FunctionEvalInfo *ei);
typedef void		(*FuncUnlinkHandle) 	(FunctionEvalInfo *ei);
typedef gboolean (*FunctionGetFullInfoCallback) (FunctionDefinition *fn_def,
                                                 gchar const **args_ptr,
                                                 gchar const **arg_names_ptr,
                                                 gchar const ***help_ptr,
                                                 FunctionArgs	  *fn_args_ptr,
                                                 FunctionNodes	  *fn_nodes_ptr,
						 FuncLinkHandle	  *link,
						 FuncUnlinkHandle *unlink);

struct _FunctionDefinition {
	FunctionGetFullInfoCallback get_full_info_callback;
	FunctionFlags flags;
	gchar   const *name;
	gchar   const *named_arguments;
	gchar   const **help;
	FuncType       fn_type;
	union {
		FunctionNodes fn_nodes;
		struct {
			char const *arg_spec;
			FunctionArgs  func;
			int min_args, max_args;
			char *arg_types;
		} args;
	} fn;
	FuncLinkHandle   link;
	FuncUnlinkHandle unlink;
	gpointer     user_data;
	gint         ref_count;
};
void func_ref	 (FunctionDefinition *fn_def);
void func_unref  (FunctionDefinition *fn_def);
gint func_get_ref_count (FunctionDefinition *fn_def);

void func_def_load (FunctionDefinition *fn_def);

FunctionDefinition *func_lookup_by_name	(gchar const *fn_name,
                                         Workbook const *optional_scope);
void                function_remove     (FunctionCategory *category,
                                         gchar const *name);
FunctionDefinition *function_add_args	(FunctionCategory *category,
                                         gchar const *name,
                                         gchar const *args,
                                         gchar const *arg_names,
                                         gchar const **help,
                                         FunctionArgs fn);
FunctionDefinition *function_add_nodes	(FunctionCategory *category,
                                         gchar const *name,
                                         gchar const *args,
                                         gchar const *arg_names,
                                         gchar const **help,
                                         FunctionNodes fn);
FunctionDefinition *function_add_name_only (FunctionCategory *category,
                                            gchar const *name,
                                            FunctionGetFullInfoCallback callback);
FunctionDefinition *function_add_placeholder (gchar const *name,
                                              gchar const *type);

gpointer function_def_get_user_data    (FunctionDefinition const *fn_def);
void     function_def_set_user_data    (FunctionDefinition *fn_def,
                                        gpointer user_data);

char const *function_def_get_name      (FunctionDefinition const *fn_def);
void        function_def_count_args    (FunctionDefinition const *fn_def,
                                        gint *min, int *max);
char        function_def_get_arg_type  (FunctionDefinition const *fn_def,
                                        gint arg_idx);
char const *function_def_get_arg_type_string  (FunctionDefinition const *fn_def,
                                        gint arg_idx);
char       *function_def_get_arg_name  (FunctionDefinition const *fn_def,
                                        gint arg_idx);

void function_set_link_handlers (FunctionDefinition *fn_def,
				 FuncLinkHandle   link,
				 FuncUnlinkHandle unlink);

Value *function_call_with_list	     (FunctionEvalInfo *ei, GnmExprList *args);
Value *function_call_with_values     (EvalPos const *ep, gchar const *name,
                                      gint argc, Value *values []);
Value *function_def_call_with_values (EvalPos const *ep, FunctionDefinition const *fn,
                                      gint argc, Value *values []);

/* Utilies to interate through ranges and argument lists */
typedef Value * (*FunctionIterateCB) (EvalPos const *ep,
                                      Value *value, gpointer user_data);
Value *function_iterate_argument_values	(EvalPos const	   *ep,
                                         FunctionIterateCB  cb,
                                         gpointer           user_data,
                                         GnmExprList          *expr_node_list,
                                         gboolean           strict,
                                         gboolean           ignore_blank);
Value *function_iterate_do_value	(EvalPos const      *ep,
                                     FunctionIterateCB   cb,
                                     gpointer            user_data,
                                     Value              *value,
                                     gboolean            strict,
                                     gboolean            ignore_blank);

/******************************************************************************/

/* Detailed function help */
typedef struct {
	GPtrArray *sections;
	gboolean   help_is_localized;
	gchar     *help_copy;
	FunctionDefinition const *fndef;
} TokenizedHelp;

TokenizedHelp *tokenized_help_new     (FunctionDefinition const *fn_def);
gchar   const *tokenized_help_find    (TokenizedHelp *tok, gchar const *token);
void           tokenized_help_destroy (TokenizedHelp *tok);

#endif /* GNUMERIC_FUNC_H */

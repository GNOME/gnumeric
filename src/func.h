#ifndef GNUMERIC_FUNC_H
#define GNUMERIC_FUNC_H

#include <gnome.h>
#include "gnumeric.h"
#include "value.h"
#include "expr.h"

/* Setup of the symbol table */
void functions_init     (void);
/* Used to build manual */
void function_dump_defs (const char *filename);

/******************************************************************************/

/* Function category support */
typedef struct _FunctionCategory FunctionCategory;
struct _FunctionCategory {
	String const *name;
	GList *functions;
};
FunctionCategory *function_get_category     (gchar const *description);
FunctionCategory *function_category_get_nth (int const n);
void function_category_add_func (FunctionCategory *, FunctionDefinition *);

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
	FUNCTION_RECALC_ALWAYS = 0x02, /* eg now() */

	/* For functions that are not exactly compatible with various import
	 * formats.  We need to recalc their results to avoid changing values
	 * unexpectedly when we recalc later.  This probably needs to be done
	 * on a per import format basis.  It may not belong here.
	 */
	FUNCTION_RECALC_ONLOAD = 0x04

	/* TODO : Are there other forms or recalc we need to think about ? */
} FunctionFlags;

struct _FunctionDefinition {
	FunctionFlags flags;
	char  const *name;
	char  const *args;
	char  const *named_arguments;
	char       **help;
	FuncType     fn_type;
	union {
		FunctionNodes *fn_nodes;
		FunctionArgs  *fn_args;
	} fn;
	gpointer     user_data;
	int 	     ref_count;
};

void func_ref	(FunctionDefinition *fn_def);
void func_unref (FunctionDefinition *fn_def);

FunctionDefinition *func_lookup_by_name	(gchar const *fn_name,
					 Workbook const *optional_scope);
FunctionDefinition *function_add_args	(FunctionCategory *parent,
					 char const *name,
					 char const *args,
					 char const *arg_names,
					 char **help,
					 FunctionArgs *fn);
FunctionDefinition *function_add_nodes	(FunctionCategory *parent,
					 char const *name,
					 char const *args,
					 char const *arg_names,
					 char **help,
					 FunctionNodes *fn);
FunctionDefinition *function_add_placeholder (char const *name, char const *type);

gpointer function_def_get_user_data    (const FunctionDefinition *fndef);
void     function_def_set_user_data    (FunctionDefinition *fndef,
					gpointer user_data);

const char *function_def_get_name      (const FunctionDefinition *fndef);
void        function_def_count_args    (const FunctionDefinition *fndef,
					int *min, int *max);
char        function_def_get_arg_type  (const FunctionDefinition *fndef,
					int arg_idx);

Value *function_call_with_list	     (FunctionEvalInfo *ei, GList *args);
Value *function_call_with_values     (const EvalPos *ep, const char *name,
				      int argc, Value *values []);
Value *function_def_call_with_values (const EvalPos *ep, FunctionDefinition *fn,
				      int argc, Value *values []);

/* Utilies to interate through ranges and argument lists */
typedef Value * (*FunctionIterateCB) (const EvalPos *ep,
				      Value *value, gpointer user_data);
Value *function_iterate_argument_values	(const EvalPos     *ep,
					 FunctionIterateCB  cb,
					 gpointer           user_data,
					 GList             *expr_node_list,
					 gboolean           strict,
					 gboolean	    ignore_blank);
Value *function_iterate_do_value	(const EvalPos      *ep,
					 FunctionIterateCB   cb,
					 gpointer            user_data,
					 Value              *value,
					 gboolean            strict,
					 gboolean	     ignore_blank);

/******************************************************************************/

/* Detailed function help */
typedef struct {
	GPtrArray *sections;
	char      *help_copy;
	FunctionDefinition const *fndef;
} TokenizedHelp;

TokenizedHelp *tokenized_help_new     (FunctionDefinition const *fndef);
const char    *tokenized_help_find    (TokenizedHelp *tok, const char *token);
void           tokenized_help_destroy (TokenizedHelp *tok);

#endif /* GNUMERIC_FUNC_H */

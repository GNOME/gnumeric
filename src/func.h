#ifndef GNUMERIC_FUNC_H
#define GNUMERIC_FUNC_H

#include "gnumeric.h"
#include "value.h"
#include "expr.h"

#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>

/* Setup of the symbol table */
void functions_init     (void);
/* Used to build manual */
void function_dump_defs (const char *filename);

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
FunctionCategory *function_category_get_nth (gint const n);
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

void func_ref	(FunctionDefinition *fn_def);
void func_unref (FunctionDefinition *fn_def);
gint func_get_ref_count (FunctionDefinition *fn_def);

typedef gboolean (*FunctionGetFullInfoCallback) (FunctionDefinition *fn_def,
                                                 gchar **args_ptr,
                                                 gchar **arg_names_ptr,
                                                 gchar ***help_ptr,
                                                 FunctionArgs **fn_args_ptr,
                                                 FunctionNodes **fn_nodes_ptr);

FunctionDefinition *func_lookup_by_name	(gchar const *fn_name,
                                         Workbook const *optional_scope);
void                function_remove     (FunctionCategory *category,
                                         gchar const *name);
FunctionDefinition *function_add_args	(FunctionCategory *category,
                                         gchar const *name,
                                         gchar const *args,
                                         gchar const *arg_names,
                                         gchar **help,
                                         FunctionArgs *fn);
FunctionDefinition *function_add_nodes	(FunctionCategory *category,
                                         gchar const *name,
                                         gchar const *args,
                                         gchar const *arg_names,
                                         gchar **help,
                                         FunctionNodes *fn);
FunctionDefinition *function_add_name_only (FunctionCategory *category,
                                            gchar const *name,
                                            FunctionGetFullInfoCallback callback);
FunctionDefinition *function_add_placeholder (gchar const *name,
                                              gchar const *type);

gpointer function_def_get_user_data    (FunctionDefinition const *fn_def);
void     function_def_set_user_data    (FunctionDefinition *fn_def,
                                        gpointer user_data);

const char *function_def_get_name      (FunctionDefinition const *fn_def);
void        function_def_count_args    (FunctionDefinition const *fn_def,
                                        gint *min, int *max);
char        function_def_get_arg_type  (FunctionDefinition const *fn_def,
                                        gint arg_idx);

Value *function_call_with_list	     (FunctionEvalInfo *ei, ExprList *args);
Value *function_call_with_values     (const EvalPos *ep, const gchar *name,
                                      gint argc, Value *values []);
Value *function_def_call_with_values (EvalPos const *ep, FunctionDefinition const *fn,
                                      gint argc, Value *values []);

/* Utilies to interate through ranges and argument lists */
typedef Value * (*FunctionIterateCB) (const EvalPos *ep,
                                      Value *value, gpointer user_data);
Value *function_iterate_argument_values	(const EvalPos     *ep,
                                         FunctionIterateCB  cb,
                                         gpointer           user_data,
                                         ExprList          *expr_node_list,
                                         gboolean           strict,
                                         gboolean           ignore_blank);
Value *function_iterate_do_value	(const EvalPos      *ep,
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
const gchar   *tokenized_help_find    (TokenizedHelp *tok, const gchar *token);
void           tokenized_help_destroy (TokenizedHelp *tok);

#endif /* GNUMERIC_FUNC_H */

#ifndef GNUMERIC_EXPR_NAME_H
#define GNUMERIC_EXPR_NAME_H

#include "gnumeric.h"
#include "expr.h"
#include "parse-util.h"

struct _NamedExpression {
	int	    ref_count;
	String     *name;
	ParsePos    pos;
	GHashTable *dependents;

	gboolean    builtin;
	union {
		ExprTree     *expr_tree;
		FunctionArgs *expr_func;
	} t;
};

NamedExpression *expr_name_lookup (ParsePos const *pos, char const *name);
NamedExpression *expr_name_add    (ParsePos const *pp, char const *name,
				   ExprTree *expr, char **error_msg);
NamedExpression *expr_name_create (ParsePos const *pp, char const *name,
				   char const *expr_str, ParseError *error);

void	 expr_name_ref	      (NamedExpression *exprn);
void	 expr_name_unref      (NamedExpression *exprn);
void     expr_name_remove     (NamedExpression *exprn);
Value   *expr_name_eval       (NamedExpression const *ne,
			       EvalPos const* pos, ExprEvalFlags flags);
char    *expr_name_as_string  (NamedExpression const *ne, ParsePos const *pp);
gboolean expr_name_set_scope  (NamedExpression *ne, Sheet *sheet);
void	 expr_name_set_expr   (NamedExpression *ne, ExprTree *new_expr);
void	 expr_name_add_dep    (NamedExpression *ne, Dependent *dep);
void	 expr_name_remove_dep (NamedExpression *ne, Dependent *dep);

GList	 *expr_name_list_destroy	  (GList *names);
void      expr_name_invalidate_refs_name  (NamedExpression *ne);
void      expr_name_invalidate_refs_sheet (Sheet const *sheet);
void      expr_name_invalidate_refs_wb	  (Workbook const *wb);

void expr_name_init       (void);

GList *sheet_get_available_names (Sheet const *sheet);

#endif /* GNUMERIC_EXPR_NAME_H */

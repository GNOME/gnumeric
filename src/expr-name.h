#ifndef GNUMERIC_EXPR_NAME_H
#define GNUMERIC_EXPR_NAME_H

#include "gnumeric.h"
#include "expr.h"
#include "func.h"
#include "parse-util.h"

struct _GnmNamedExpr {
	int	    ref_count;
	String     *name;
	ParsePos    pos;
	GHashTable *dependents;
	unsigned char active;
	unsigned char builtin;
	union {
		GnmExpr const *expr_tree;
		GnmFuncArgs    expr_func;
	} t;
};

GnmNamedExpr *expr_name_lookup (ParsePos const *pos, char const *name);

GnmNamedExpr *expr_name_new    (char const *name, gboolean builtin);
GnmNamedExpr *expr_name_add    (ParsePos const *pp, char const *name,
				GnmExpr const *expr, char const **error_msg);

void	 expr_name_ref	      (GnmNamedExpr *exprn);
void	 expr_name_unref      (GnmNamedExpr *exprn);
void     expr_name_remove     (GnmNamedExpr *exprn);
Value   *expr_name_eval       (GnmNamedExpr const *ne, EvalPos const *ep,
			       GnmExprEvalFlags flags);
char    *expr_name_as_string  (GnmNamedExpr const *ne, ParsePos const *pp);
gboolean expr_name_set_scope  (GnmNamedExpr *ne, Sheet *sheet);
void	 expr_name_set_expr   (GnmNamedExpr *ne, GnmExpr const *new_expr,
			       GnmExprRewriteInfo const *rwinfo);
void	 expr_name_add_dep    (GnmNamedExpr *ne, Dependent *dep);
void	 expr_name_remove_dep (GnmNamedExpr *ne, Dependent *dep);
gboolean expr_name_is_placeholder (GnmNamedExpr const *ne);

void expr_name_list_destroy (GList **names);

void expr_name_init       (void);
void expr_name_shutdown   (void);

GList	   *sheet_names_get_available (Sheet const *sheet);
char const *sheet_names_check	      (Sheet const *sheet, Range const *r);

#endif /* GNUMERIC_EXPR_NAME_H */

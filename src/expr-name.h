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
	GnmExpr const *expr_tree;
	gboolean    active;
	gboolean    is_placeholder;
	gboolean    is_hidden;
};

GnmNamedExpr *expr_name_lookup (ParsePos const *pos, char const *name);
GnmNamedExpr *expr_name_add    (ParsePos const *pp, char const *name,
				GnmExpr const *expr, char **error_msg,
				gboolean link_to_container);

void	 expr_name_ref	      (GnmNamedExpr *nexpr);
void	 expr_name_unref      (GnmNamedExpr *nexpr);
void     expr_name_remove     (GnmNamedExpr *nexpr);
Value   *expr_name_eval       (GnmNamedExpr const *ne, EvalPos const *ep,
			       GnmExprEvalFlags flags);
char    *expr_name_as_string  (GnmNamedExpr const *ne, ParsePos const *pp,
			       GnmExprConventions const *fmt);
gboolean expr_name_set_scope  (GnmNamedExpr *ne, Sheet *sheet);
void	 expr_name_set_expr   (GnmNamedExpr *ne, GnmExpr const *new_expr);
void	 expr_name_add_dep    (GnmNamedExpr *ne, Dependent *dep);
void	 expr_name_remove_dep (GnmNamedExpr *ne, Dependent *dep);
gboolean expr_name_is_placeholder (GnmNamedExpr const *ne);

int      expr_name_by_name    (const GnmNamedExpr *a, const GnmNamedExpr *b);

GList	   *sheet_names_get_available (Sheet const *sheet);
char const *sheet_names_check	      (Sheet const *sheet, Range const *r);

/******************************************************************************/

struct _GnmNamedExprCollection {
	/* all the defined names */
	GHashTable *names;

	/* placeholders for references to undefined names */
	GHashTable *placeholders;
};

void gnm_named_expr_collection_free (GnmNamedExprCollection **names);

#endif /* GNUMERIC_EXPR_NAME_H */

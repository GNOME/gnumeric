#ifndef GNUMERIC_EXPR_NAME_H
#define GNUMERIC_EXPR_NAME_H

#include "gnumeric.h"
#include "expr.h"
#include "func.h"
#include "parse-util.h"

struct _GnmNamedExpr {
	int	    ref_count;
	GnmString  *name;
	GnmParsePos    pos;
	GHashTable *dependents;
	GnmExpr const *expr;
	gboolean    active;
	gboolean    is_placeholder;
	gboolean    is_hidden;
};

GnmNamedExpr *expr_name_lookup (GnmParsePos const *pos, char const *name);
GnmNamedExpr *expr_name_add    (GnmParsePos const *pp, char const *name,
				GnmExpr const *expr, char **error_msg,
				gboolean link_to_container);

void	 expr_name_ref	      (GnmNamedExpr *nexpr);
void	 expr_name_unref      (GnmNamedExpr *nexpr);
void     expr_name_remove     (GnmNamedExpr *nexpr);
GnmValue*expr_name_eval       (GnmNamedExpr const *ne, GnmEvalPos const *ep,
			       GnmExprEvalFlags flags);
char    *expr_name_as_string  (GnmNamedExpr const *ne, GnmParsePos const *pp,
			       GnmExprConventions const *fmt);
char    *expr_name_set_scope  (GnmNamedExpr *ne, Sheet *sheet);
void	 expr_name_set_expr   (GnmNamedExpr *ne, GnmExpr const *new_expr);
void	 expr_name_add_dep    (GnmNamedExpr *ne, GnmDependent *dep);
void	 expr_name_remove_dep (GnmNamedExpr *ne, GnmDependent *dep);
gboolean expr_name_is_placeholder (GnmNamedExpr const *ne);
void	 expr_name_downgrade_to_placeholder (GnmNamedExpr *nexpr);

int      expr_name_cmp_by_name    (GnmNamedExpr const *a, GnmNamedExpr const *b);
gboolean expr_name_check_for_loop (char const *name, GnmExpr const *expr);

GList	   *sheet_names_get_available (Sheet const *sheet);
char const *sheet_names_check	      (Sheet const *sheet, GnmRange const *r);

/******************************************************************************/

struct _GnmNamedExprCollection {
	/* all the defined names */
	GHashTable *names;

	/* placeholders for references to undefined names */
	GHashTable *placeholders;
};

void gnm_named_expr_collection_free (GnmNamedExprCollection **names);

#endif /* GNUMERIC_EXPR_NAME_H */

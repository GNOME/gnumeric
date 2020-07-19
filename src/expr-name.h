#ifndef _GNM_EXPR_NAME_H_
# define _GNM_EXPR_NAME_H_

#include <gnumeric.h>
#include <position.h>
#include <goffice/goffice.h>

G_BEGIN_DECLS

struct _GnmNamedExpr {
	int	    ref_count;
	GOString   *name;
	GnmParsePos    pos;
	GHashTable *dependents;
	GnmExprTop const *texpr;
	gboolean    is_placeholder;
	gboolean    is_hidden;
	gboolean    is_permanent;
	gboolean    is_editable;
	GnmNamedExprCollection *scope;
};

gboolean expr_name_validate (const char *name);

GType         gnm_named_expr_get_type (void);
GnmNamedExpr *expr_name_new    (char const *name);
GnmNamedExpr *expr_name_lookup (GnmParsePos const *pos, char const *name);
GnmNamedExpr *expr_name_add    (GnmParsePos const *pp, char const *name,
				GnmExprTop const *texpr, char **error_msg,
				gboolean link_to_container,
				GnmNamedExpr *stub);
void expr_name_perm_add        (Sheet *sheet,
				char const *name,
				GnmExprTop const *texpr,
				gboolean is_editable);
GnmNamedExpr *expr_name_ref   (GnmNamedExpr *nexpr);
void	 expr_name_unref      (GnmNamedExpr *nexpr);
void     expr_name_remove     (GnmNamedExpr *nexpr);
GnmValue*expr_name_eval       (GnmNamedExpr const *nexpr,
			       GnmEvalPos const *pos,
			       GnmExprEvalFlags flags);

const char *expr_name_name    (GnmNamedExpr const *nexpr);
gboolean expr_name_set_name   (GnmNamedExpr *nexpr, const char *new_name);

gboolean expr_name_is_placeholder (GnmNamedExpr const *ne);
void expr_name_set_is_placeholder (GnmNamedExpr *nexpr, gboolean is_placeholder);

char    *expr_name_as_string  (GnmNamedExpr const *nexpr, GnmParsePos const *pp,
			       GnmConventions const *fmt);
char    *expr_name_set_pos    (GnmNamedExpr *nexpr, GnmParsePos const *pp);
void	 expr_name_set_expr   (GnmNamedExpr *nexpr, GnmExprTop const *texpr);
void	 expr_name_add_dep    (GnmNamedExpr *nexpr, GnmDependent *dep);
void	 expr_name_remove_dep (GnmNamedExpr *nexpr, GnmDependent *dep);
gboolean expr_name_is_active  (GnmNamedExpr const *nexpr);
void	 expr_name_downgrade_to_placeholder (GnmNamedExpr *nexpr);
gboolean expr_name_in_use     (GnmNamedExpr *nexpr);

int      expr_name_cmp_by_name    (GnmNamedExpr const *a, GnmNamedExpr const *b);
gboolean expr_name_check_for_loop (char const *name, GnmExprTop const *texpr);

char const *sheet_names_check	      (Sheet const *sheet, GnmRange const *r);

GOUndo *expr_name_set_expr_undo_new (GnmNamedExpr *nexpr);

/******************************************************************************/

GType gnm_named_expr_collection_get_type (void);
GnmNamedExprCollection *gnm_named_expr_collection_new (void);
void gnm_named_expr_collection_unref (GnmNamedExprCollection *names);
void gnm_named_expr_collection_unlink (GnmNamedExprCollection *names);
void gnm_named_expr_collection_relink (GnmNamedExprCollection *names);
void gnm_named_expr_collection_foreach (GnmNamedExprCollection *names,
					GHFunc func,
					gpointer data);
GSList  *gnm_named_expr_collection_list (GnmNamedExprCollection const *scope);

GnmNamedExpr *gnm_named_expr_collection_lookup (GnmNamedExprCollection const *scope,
						char const *name);
void gnm_named_expr_collection_dump (GnmNamedExprCollection *names,
				     const char *id);
gboolean gnm_named_expr_collection_sanity_check (GnmNamedExprCollection *names,
						 const char *id);

G_END_DECLS

#endif /* _GNM_EXPR_NAME_H_ */

#ifndef GNUMERIC_EXPR_H
#define GNUMERIC_EXPR_H

#include "gnumeric.h"
#include "position.h"

/* Warning: if you add something here, see do_expr_as_string * ! */
typedef enum {
	GNM_EXPR_OP_EQUAL,	/* Compare value equal */
	GNM_EXPR_OP_GT,		/* Compare value greather than  */
	GNM_EXPR_OP_LT,		/* Compare value less than */
	GNM_EXPR_OP_GTE,	/* Compare value greather or equal than */
	GNM_EXPR_OP_LTE,	/* Compare value less or equal than */
	GNM_EXPR_OP_NOT_EQUAL,	/* Compare for non equivalence */

	GNM_EXPR_OP_ADD,	/* Add  */
	GNM_EXPR_OP_SUB,	/* Subtract */
	GNM_EXPR_OP_MULT,	/* Multiply */
	GNM_EXPR_OP_DIV,	/* Divide */
	GNM_EXPR_OP_EXP,	/* Exponentiate */
	GNM_EXPR_OP_CAT,	/* String concatenation */

	GNM_EXPR_OP_FUNCALL,	/* Function call invocation */

	GNM_EXPR_OP_NAME,	/* Name reference */

        GNM_EXPR_OP_CONSTANT,	/* Constant value */
	GNM_EXPR_OP_CELLREF,	/* Cell content lookup (variable) */
	GNM_EXPR_OP_UNARY_NEG,	/* Sign inversion */
	GNM_EXPR_OP_UNARY_PLUS,	/* Mark as positive */
	GNM_EXPR_OP_PERCENTAGE,	/* Percentage (value/100) */
	GNM_EXPR_OP_ARRAY,	/* Array access */
	GNM_EXPR_OP_SET,	/* A set of expressions */
	GNM_EXPR_OP_RANGE_CTOR,	/* A constructed range eg A1:index(1,2) */
	GNM_EXPR_OP_INTERSECT	/* The intersection of multiple ranges */
} GnmExprOp;

/* Shorthands for case statements.  Easy to read, easy to maintain.  */
#define GNM_EXPR_OP_ANY_BINARY GNM_EXPR_OP_EQUAL: case GNM_EXPR_OP_GT: case GNM_EXPR_OP_LT: case GNM_EXPR_OP_GTE: \
	case GNM_EXPR_OP_LTE: case GNM_EXPR_OP_NOT_EQUAL: \
	case GNM_EXPR_OP_ADD: case GNM_EXPR_OP_SUB: case GNM_EXPR_OP_MULT: case GNM_EXPR_OP_DIV: \
	case GNM_EXPR_OP_EXP: case GNM_EXPR_OP_CAT
#define GNM_EXPR_OP_ANY_UNARY GNM_EXPR_OP_UNARY_NEG: case GNM_EXPR_OP_UNARY_PLUS : case GNM_EXPR_OP_PERCENTAGE

GnmExpr const *gnm_expr_new_constant	(Value *v);
GnmExpr const *gnm_expr_new_unary	(GnmExprOp op, GnmExpr const *e);
GnmExpr const *gnm_expr_new_binary	(GnmExpr const *l, GnmExprOp op,
					 GnmExpr const *r);
GnmExpr const *gnm_expr_new_funcall	(FunctionDefinition *func,
					 GnmExprList *args);
GnmExpr const *gnm_expr_new_name	(GnmNamedExpr *name,
					 Sheet *sheet_scope, Workbook *wb_scope);
GnmExpr const *gnm_expr_new_cellref	(CellRef const *cr);
GnmExpr const *gnm_expr_new_array	(int x, int y, int cols, int rows,
					 GnmExpr const *expr);
GnmExpr const *gnm_expr_new_set		(GnmExprList *args);

GnmExpr const *gnm_expr_first_func   (GnmExpr const *expr);
Value	      *gnm_expr_get_range    (GnmExpr const *expr) ;
FunctionDefinition *gnm_expr_get_func_def (GnmExpr const *expr);

void	  gnm_expr_ref		     (GnmExpr const *expr);
void	  gnm_expr_unref	     (GnmExpr const *expr);
gboolean  gnm_expr_is_shared 	     (GnmExpr const *expr);
gboolean  gnm_expr_equal	     (GnmExpr const *a, GnmExpr const *b);
char	 *gnm_expr_as_string	     (GnmExpr const *expr, ParsePos const *fp);
void	  gnm_expr_get_boundingbox   (GnmExpr const *expr, Range *bound);
GSList	 *gnm_expr_referenced_sheets (GnmExpr const *expr);

struct _GnmExprRelocateInfo {
	EvalPos pos;

	Range   origin;	        /* References to cells in origin_sheet!range */
	Sheet  *origin_sheet;	/* should to adjusted */

	Sheet  *target_sheet;	/* to point at this sheet */
	int col_offset, row_offset;/* and offset by this amount */
};

struct _GnmExprRewriteInfo {
	enum {
		GNM_EXPR_REWRITE_SHEET,
		GNM_EXPR_REWRITE_WORKBOOK,
		GNM_EXPR_REWRITE_RELOCATE
	} type;
	union {
		Sheet const     *sheet;
		Workbook const  *workbook;
		GnmExprRelocateInfo relocate;
	} u;
};

GnmExpr const *gnm_expr_rewrite (GnmExpr            const *expr,
				 GnmExprRewriteInfo const *rwinfo);

Value *gnm_expr_eval (GnmExpr const *expr, EvalPos const *pos,
		      GnmExprEvalFlags flags);

/*****************************************************************************/

#define gnm_expr_list_append(l,e)  g_slist_append ((l), (gpointer)(e))
#define gnm_expr_list_prepend(l,e) g_slist_prepend ((l), (gpointer)(e))
#define gnm_expr_list_length	   g_slist_length
#define gnm_expr_list_free	   g_slist_free
void 	 gnm_expr_list_unref	  (GnmExprList *list);
char    *gnm_expr_list_as_string  (GnmExprList const *list, ParsePos const *p);
gboolean gnm_expr_list_equal	  (GnmExprList const *a, GnmExprList const *b);

/*****************************************************************************/

typedef struct {
	GHashTable *exprs, *ptrs;

	int nodes_in, nodes_stored;
} ExprTreeSharer;

ExprTreeSharer *expr_tree_sharer_new     (void);
void            expr_tree_sharer_destroy (ExprTreeSharer *);
GnmExpr const  *expr_tree_sharer_share   (ExprTreeSharer *, GnmExpr const *expr);

void expr_init (void);
void expr_shutdown (void);

#endif /* GNUMERIC_EXPR_H */

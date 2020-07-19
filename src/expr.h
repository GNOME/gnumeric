#ifndef _GNM_EXPR_H_
# define _GNM_EXPR_H_

G_BEGIN_DECLS

#include <gnumeric.h>
#include <position.h>

/* Warning: if you add something here, see do_expr_as_string   ! */
/* Warning: if you add something here, see ms-formula-write.c  ! */
typedef enum {
	GNM_EXPR_OP_PAREN,	/* Parentheses for clarity */
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
	GNM_EXPR_OP_ARRAY_CORNER,/* Top Corner of an array */
	GNM_EXPR_OP_ARRAY_ELEM,	/* General Array element */
	GNM_EXPR_OP_SET,	/* A set of expressions */
	GNM_EXPR_OP_RANGE_CTOR,	/* A constructed range eg A1:index(1,2) */
	GNM_EXPR_OP_INTERSECT	/* The intersection of multiple ranges */
} GnmExprOp;

/* Shorthands for case statements.  Easy to read, easy to maintain.  */
#define GNM_EXPR_OP_ANY_BINARY GNM_EXPR_OP_EQUAL: case GNM_EXPR_OP_GT: case GNM_EXPR_OP_LT: case GNM_EXPR_OP_GTE: \
	case GNM_EXPR_OP_LTE: case GNM_EXPR_OP_NOT_EQUAL: \
	case GNM_EXPR_OP_ADD: case GNM_EXPR_OP_SUB: case GNM_EXPR_OP_MULT: case GNM_EXPR_OP_DIV: \
	case GNM_EXPR_OP_EXP: case GNM_EXPR_OP_CAT
#define GNM_EXPR_OP_ANY_UNARY GNM_EXPR_OP_PAREN: case GNM_EXPR_OP_UNARY_NEG: case GNM_EXPR_OP_UNARY_PLUS: case GNM_EXPR_OP_PERCENTAGE

GType          gnm_expr_get_type        (void);
GType	       gnm_expr_array_corner_get_type (void); /* boxed type */
GnmExpr const *gnm_expr_new_constant	(GnmValue *v);
GnmExpr const *gnm_expr_new_unary	(GnmExprOp op, GnmExpr const *e);
GnmExpr const *gnm_expr_new_binary	(GnmExpr const *l, GnmExprOp op,
					 GnmExpr const *r);
GnmExpr const *gnm_expr_new_funcall	(GnmFunc *func,
					 GnmExprList *args);
GnmExpr const *gnm_expr_new_funcall1	(GnmFunc *func,
					 GnmExpr const *arg0);
GnmExpr const *gnm_expr_new_funcall2	(GnmFunc *func,
					 GnmExpr const *arg0,
					 GnmExpr const *arg1);
GnmExpr const *gnm_expr_new_funcall3	(GnmFunc *func,
					 GnmExpr const *arg0,
					 GnmExpr const *arg1,
					 GnmExpr const *arg2);
GnmExpr const *gnm_expr_new_funcall4	(GnmFunc *func,
					 GnmExpr const *arg0,
					 GnmExpr const *arg1,
					 GnmExpr const *arg2,
					 GnmExpr const *arg3);
GnmExpr const *gnm_expr_new_funcall5	(GnmFunc *func,
					 GnmExpr const *arg0,
					 GnmExpr const *arg1,
					 GnmExpr const *arg2,
					 GnmExpr const *arg3,
					 GnmExpr const *arg4);
GnmExpr const *gnm_expr_new_name	(GnmNamedExpr *name,
					 Sheet *sheet_scope, Workbook *wb_scope);
GnmExpr const *gnm_expr_new_cellref	(GnmCellRef const *cr);
GnmExpr const *gnm_expr_new_set		(GnmExprList *args);
GnmExpr const *gnm_expr_new_range_ctor  (GnmExpr const *l, GnmExpr const *r);

GnmValue      *gnm_expr_get_range    (GnmExpr const *expr);
GnmFunc       *gnm_expr_get_func_def (GnmExpr const *expr);
GnmExpr const *gnm_expr_get_func_arg (GnmExpr const *expr, int i);

void	  gnm_expr_free              (GnmExpr const *expr);
GnmExpr const *gnm_expr_copy         (GnmExpr const *expr);
gboolean  gnm_expr_equal (GnmExpr const *a, GnmExpr const *b);
gboolean  gnm_expr_is_rangeref	     (GnmExpr const *expr);
gboolean  gnm_expr_is_data_table     (GnmExpr const *expr,
				      GnmCellPos *c_in, GnmCellPos *r_in);
gboolean  gnm_expr_is_empty          (GnmExpr const *expr);

GnmValue const *gnm_expr_get_constant	(GnmExpr const *expr);

GnmNamedExpr const *gnm_expr_get_name	(GnmExpr const *expr);

GnmCellRef const*gnm_expr_get_cellref	(GnmExpr const *expr);

void	  gnm_expr_as_gstring	     (GnmExpr const *expr,
				      GnmConventionsOut *out);
char	 *gnm_expr_as_string	     (GnmExpr const *expr, GnmParsePos const *pp,
				      GnmConventions const *convs);
void      gnm_expr_list_as_string    (int argc, GnmExprConstPtr const *argv,
				      GnmConventionsOut *out);
gboolean  gnm_expr_contains_subtotal (GnmExpr const *expr);

GnmValue *gnm_expr_eval (GnmExpr const *expr, GnmEvalPos const *pos,
			 GnmExprEvalFlags flags);

GnmExpr const *gnm_expr_simplify_if  (GnmExpr const *expr);

typedef struct GnmExprWalk_ {
	/* User data */
	gpointer user;

	/* Flags the walker callback can use to signal the engine.  */
	gboolean stop;

	/* Internal flags.  */
	guint flags;
} GnmExprWalk;
typedef GnmExpr const * (*GnmExprWalkerFunc) (GnmExpr const *expr, GnmExprWalk *data);
GnmExpr const *gnm_expr_walk (GnmExpr const *expr, GnmExprWalkerFunc walker, gpointer user);

/*****************************************************************************/

#define gnm_expr_list_append(l,e)  g_slist_append ((l), (gpointer)(e))
#define gnm_expr_list_prepend(l,e) g_slist_prepend ((l), (gpointer)(e))
#define gnm_expr_list_length(l)	   g_slist_length((GSList *)(l)) /* const cast */
#define gnm_expr_list_free	   g_slist_free
void	 gnm_expr_list_unref	  (GnmExprList *list);
GnmExprList *gnm_expr_list_copy	  (GnmExprList *list);

/*****************************************************************************/

#define GNM_EXPR_TOP_MAGIC 0x42
#define GNM_IS_EXPR_TOP(et) ((et) && (et)->magic == GNM_EXPR_TOP_MAGIC)

struct _GnmExprTop {
	unsigned magic : 8;
	unsigned hash : 24;  /* Zero meaning not yet computed.  */
	guint32 refcount;
	GnmExpr const *expr;
};

GnmExprTop const *gnm_expr_top_new		(GnmExpr const *e);
GnmExprTop const *gnm_expr_top_new_constant	(GnmValue *v);
GnmExprTop const *gnm_expr_top_new_array_corner (int cols, int rows, GnmExpr const *expr);
GnmExprTop const *gnm_expr_top_new_array_elem	(int x, int y);

GType		gnm_expr_top_get_type (void);
GnmExprTop const *gnm_expr_top_ref		(GnmExprTop const *texpr);
void		gnm_expr_top_unref		(GnmExprTop const *texpr);
gboolean	gnm_expr_top_equal		(GnmExprTop const *te1, GnmExprTop const *te2);
guint           gnm_expr_top_hash               (GnmExprTop const *texpr);
gboolean	gnm_expr_top_is_shared		(GnmExprTop const *texpr);
gboolean	gnm_expr_top_is_err		(GnmExprTop const *texpr, GnmStdError e);
gboolean	gnm_expr_top_is_rangeref	(GnmExprTop const *texpr);
gboolean	gnm_expr_top_is_array_elem	(GnmExprTop const *texpr, int *x, int *y);
gboolean	gnm_expr_top_is_array_corner	(GnmExprTop const *texpr);
gboolean	gnm_expr_top_is_array		(GnmExprTop const *texpr);
void		gnm_expr_top_get_array_size	(GnmExprTop const *texpr, int *cols, int *rows);
GnmValue       *gnm_expr_top_get_array_value	(GnmExprTop const *texpr);
GnmExpr const  *gnm_expr_top_get_array_expr	(GnmExprTop const *texpr);
GnmValue       *gnm_expr_top_get_range		(GnmExprTop const *texpr);
GSList	       *gnm_expr_top_get_ranges		(GnmExprTop const *texpr);
GnmValue const *gnm_expr_top_get_constant	(GnmExprTop const *texpr);
GnmCellRef const*gnm_expr_top_get_cellref	(GnmExprTop const *texpr);
void		gnm_expr_top_get_boundingbox	(GnmExprTop const *texpr,
						 Sheet const *sheet,
						 GnmRange *bound);
gboolean	gnm_expr_top_contains_subtotal	(GnmExprTop const *texpr);
gboolean	gnm_expr_top_is_volatile	(GnmExprTop const *texpr);
GSList	       *gnm_expr_top_referenced_sheets	(GnmExprTop const *texpr);
GnmExpr const  *gnm_expr_top_first_funcall	(GnmExprTop const *texpr);
GnmExprTop const *gnm_expr_top_transpose        (GnmExprTop const *texpr);

struct _GnmExprRelocateInfo {
	GnmParsePos pos;

	GnmRange   origin;	    /* References to cells in origin_sheet!range */
	Sheet     *origin_sheet;    /* should to adjusted */
	Sheet     *target_sheet;    /* to point at this sheet */
	int col_offset, row_offset; /* and offset by this amount */
	enum {
		/* invalidate references to any sheets with
		 *	Sheet::being_invalidated == TRUE */
		GNM_EXPR_RELOCATE_INVALIDATE_SHEET,
		GNM_EXPR_RELOCATE_MOVE_RANGE,
		GNM_EXPR_RELOCATE_COLS,		/* ins/del col */
		GNM_EXPR_RELOCATE_ROWS		/* ins/del row */
	} reloc_type;

	/* Valid for COLS/ROWS only.  Assumed by MOVE_RANGE.  If TRUE,
	   ranges ending at the edge of the sheet will keep the end
	   there.  */
	gboolean sticky_end;

};
GnmExprTop const *gnm_expr_top_relocate	 (GnmExprTop const *texpr,
					  GnmExprRelocateInfo const *rinfo,
					  gboolean include_rel);
GnmExprTop const * gnm_expr_top_relocate_sheet (GnmExprTop const *texpr,
						Sheet const *src,
						Sheet const *dst);

GnmValue *gnm_expr_top_eval	  (GnmExprTop const *texpr,
				   GnmEvalPos const *pos,
				   GnmExprEvalFlags flags);
char	 *gnm_expr_top_as_string  (GnmExprTop const *texpr,
				   GnmParsePos const *pp,
				   GnmConventions const *convs);
void	  gnm_expr_top_as_gstring (GnmExprTop const *texpr,
				   GnmConventionsOut *out);
char	 *gnm_expr_top_multiple_as_string  (GnmExprTop const *texpr,
					    GnmParsePos const *pp,
					    GnmConventions const *convs);

GnmValue *gnm_expr_top_eval_fake_array (GnmExprTop const *texpr,
					GnmEvalPos const *pos,
					GnmExprEvalFlags flags);

/*****************************************************************************/

GType             gnm_expr_sharer_get_type (void);
GnmExprSharer *   gnm_expr_sharer_new  (void);
void              gnm_expr_sharer_unref (GnmExprSharer *es);
GnmExprTop const *gnm_expr_sharer_share (GnmExprSharer *es, GnmExprTop const *texpr);
void              gnm_expr_sharer_report (GnmExprSharer *es);

/*****************************************************************************/

void gnm_expr_init_ (void);
void gnm_expr_shutdown_ (void);

G_END_DECLS

#endif /* _GNM_EXPR_H_ */

#ifndef GNM_EXPR_IMPL_H_
#define GNM_EXPR_IMPL_H_

#include <gnumeric.h>
#include <numbers.h>
#include <parse-util.h>


G_BEGIN_DECLS

struct GnmExprConstant_ {
	guint8 oper;
	GnmValue const *value;
};

struct GnmExprFunction_ {
	guint8 oper;
	int argc;
	GnmFunc *func;
	GnmExprConstPtr *argv;
};

struct GnmExprUnary_ {
	guint8 oper;
	GnmExpr const *value;
};

struct GnmExprBinary_ {
	guint8 oper;
	GnmExpr const *value_a;
	GnmExpr const *value_b;
};

/* We could break this out into multiple types to be more space efficient */
struct GnmExprName_ {
	guint8 oper;
	Sheet *optional_scope;
	Workbook *optional_wb_scope;
	GnmNamedExpr *name;
};

struct GnmExprCellRef_ {
	guint8 oper;
	GnmCellRef ref;
};

struct GnmExprArrayCorner_ {
	guint8 oper;
	guint32 cols, rows;
	GnmValue *value;	/* Last array result */
	GnmExpr const *expr;	/* Real Expression */
};
struct GnmExprArrayElem_ {
	guint8 oper;
	guint32 x, y;
};

struct GnmExprSet_ {
	guint8 oper;
	int argc;
	GnmExprConstPtr *argv;
};

union GnmExpr_ {
	GnmExprConstant		constant;
	GnmExprFunction		func;
	GnmExprUnary		unary;
	GnmExprBinary		binary;
	GnmExprName		name;
	GnmExprCellRef		cellref;
	GnmExprArrayCorner	array_corner;
	GnmExprArrayElem	array_elem;
	GnmExprSet		set;
};

#define GNM_EXPR_GET_OPER(e_) (0 ? (e_) == (GnmExpr const *)0 : (GnmExprOp)*(guint8*)(e_))

#define gnm_expr_constant_init(expr, val)	\
do {						\
	(expr)->oper = GNM_EXPR_OP_CONSTANT;	\
	(expr)->value = val;			\
} while (0)


struct GnmExprSharer_ {
	GHashTable *exprs;

	int nodes_in, nodes_stored, nodes_killed;
	unsigned ref_count;
};


G_END_DECLS

#endif /* GNM_EXPR_IMPL_H_ */

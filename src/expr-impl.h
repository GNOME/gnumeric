#ifndef GNUMERIC_EXPR_IMPL_H
#define GNUMERIC_EXPR_IMPL_H

#include "gnumeric.h"
#include "numbers.h"
#include "parse-util.h"



struct _GnmExprConstant {
	guint32 oper_and_refcount;
	GnmValue const *value;
};

struct _GnmExprFunction {
	guint32 oper_and_refcount;
	int argc;
	GnmFunc *func;
	GnmExprConstPtr *argv;
};

struct _GnmExprUnary {
	guint32 oper_and_refcount;
	GnmExpr const *value;
};

struct _GnmExprBinary {
	guint32 oper_and_refcount;
	GnmExpr const *value_a;
	GnmExpr const *value_b;
};

/* We could break this out into multiple types to be more space efficient */
struct _GnmExprName {
	guint32 oper_and_refcount;
	Sheet *optional_scope;
	Workbook *optional_wb_scope;
	GnmNamedExpr *name;
};

struct _GnmExprCellRef {
	guint32 oper_and_refcount;
	GnmCellRef ref;
};

struct _GnmExprArrayCorner {
	guint32 oper_and_refcount;
	guint32 cols, rows;
	GnmValue *value;	/* Last array result */
	GnmExpr const *expr;	/* Real Expression */
};
struct _GnmExprArrayElem {
	guint32 oper_and_refcount;
	guint32 x, y;
};

struct _GnmExprSet {
	guint32 oper_and_refcount;
	int argc;
	GnmExprConstPtr *argv;
};

union _GnmExpr {
	guint32                 oper_and_refcount;
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

#define GNM_EXPR_GET_REFCOUNT(e) ((e)->oper_and_refcount & ((1 << 27) - 1))
#define GNM_EXPR_GET_OPER(e) ((GnmExprOp)((e)->oper_and_refcount >> 27))
#define GNM_EXPR_SET_OPER_REF1(e,o) ((e)->oper_and_refcount = (((o) << 27) | 1))

#define gnm_expr_constant_init(expr, val)			\
do {								\
	GNM_EXPR_SET_OPER_REF1(expr, GNM_EXPR_OP_CONSTANT);	\
	(expr)->value = val;					\
} while (0)

#endif /* GNUMERIC_EXPR_IMPL_H */

#ifndef GNUMERIC_EXPR_IMPL_H
#define GNUMERIC_EXPR_IMPL_H

#include "gnumeric.h"
#include "numbers.h"
#include "parse-util.h"

struct _GnmExprConstant {
	GnmExprOp oper;
	int       ref_count;

	GnmValue const *value;
};

struct _GnmExprFunction {
	GnmExprOp oper;
	int       ref_count;

	GnmFunc     *func;
	GnmExprList *arg_list;
};

struct _GnmExprUnary {
	GnmExprOp oper;
	int       ref_count;

	GnmExpr const *value;
};

struct _GnmExprBinary {
	GnmExprOp oper;
	int       ref_count;

	GnmExpr const *value_a;
	GnmExpr const *value_b;
};

/* We could break this out into multiple types to be more space efficient */
struct _GnmExprName {
	GnmExprOp oper;
	int       ref_count;

	Sheet	     *optional_scope;
	Workbook     *optional_wb_scope;
	GnmNamedExpr *name;
};

struct _GnmExprCellRef {
	GnmExprOp oper;
	int       ref_count;

	GnmCellRef ref;
};

struct _GnmExprArrayCorner {
	GnmExprOp oper;
	int       ref_count;
	guint32	  cols, rows;
	GnmValue *value;	/* Last array result */
	GnmExpr const *expr;	/* Real Expression */
};
struct _GnmExprArrayElem {
	GnmExprOp oper;
	int       ref_count;
	guint32	  x, y;
};

struct _GnmExprSet {
	GnmExprOp oper;
	int       ref_count;

	GnmExprList *set;
};

union _GnmExpr {
	struct {
		GnmExprOp oper;
		int       ref_count;
	} any;

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

#define gnm_expr_constant_init(expr, val)	\
do {						\
	(expr)->ref_count = 1;			\
	(expr)->oper = GNM_EXPR_OP_CONSTANT;		\
	(expr)->value = val;			\
} while (0)

#endif /* GNUMERIC_EXPR_IMPL_H */

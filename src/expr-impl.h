#ifndef GNUMERIC_EXPR_IMPL_H
#define GNUMERIC_EXPR_IMPL_H

#include "gnumeric.h"
#include "numbers.h"
#include "parse-util.h"

struct _GnmExprConstant {
	GnmExprOp oper;
	int       ref_count;

	Value const *value;
};

struct _GnmExprFunction {
	GnmExprOp oper;
	int       ref_count;

	FunctionDefinition *func;
	GnmExprList        *arg_list;
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

	CellRef ref;
};

struct _GnmExprArray {
	GnmExprOp oper;
	int       ref_count;

	int x, y;
	int cols, rows;
	struct {
		/* Upper left corner */
		Value *value;		/* Last array result */
		GnmExpr const *expr;	/* Real Expression */
	} corner;
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
	GnmExprArray		array;
	GnmExprSet		set;
};

#define gnm_expr_constant_init(expr, val)	\
do {						\
	(expr)->ref_count = 1;			\
	(expr)->oper = GNM_EXPR_OP_CONSTANT;		\
	(expr)->value = val;			\
} while (0)

#endif /* GNUMERIC_EXPR_IMPL_H */

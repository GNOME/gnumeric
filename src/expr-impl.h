/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_EXPR_IMPL_H_
# define _GNM_EXPR_IMPL_H_

#include "gnumeric.h"
#include "numbers.h"
#include "parse-util.h"


G_BEGIN_DECLS

struct _GnmExprConstant {
	guint8 oper;
	GnmValue const *value;
};

struct _GnmExprFunction {
	guint8 oper;
	int argc;
	GnmFunc *func;
	GnmExprConstPtr *argv;
};

struct _GnmExprUnary {
	guint8 oper;
	GnmExpr const *value;
};

struct _GnmExprBinary {
	guint8 oper;
	GnmExpr const *value_a;
	GnmExpr const *value_b;
};

/* We could break this out into multiple types to be more space efficient */
struct _GnmExprName {
	guint8 oper;
	Sheet *optional_scope;
	Workbook *optional_wb_scope;
	GnmNamedExpr *name;
};

struct _GnmExprCellRef {
	guint8 oper;
	GnmCellRef ref;
};

struct _GnmExprArrayCorner {
	guint8 oper;
	guint32 cols, rows;
	GnmValue *value;	/* Last array result */
	GnmExpr const *expr;	/* Real Expression */
};
struct _GnmExprArrayElem {
	guint8 oper;
	guint32 x, y;
};

struct _GnmExprSet {
	guint8 oper;
	int argc;
	GnmExprConstPtr *argv;
};

union _GnmExpr {
	guint8                  oper;
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

#define GNM_EXPR_GET_OPER(e) ((GnmExprOp)((e)->oper))

#define gnm_expr_constant_init(expr, val)	\
do {						\
	(expr)->oper = GNM_EXPR_OP_CONSTANT;	\
	(expr)->value = val;			\
} while (0)


struct _GnmExprSharer {
	GHashTable *exprs;

	int nodes_in, nodes_stored, nodes_killed;
};


G_END_DECLS

#endif /* _GNM_EXPR_IMPL_H_ */

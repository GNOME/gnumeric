#ifndef GNUMERIC_EXPR_H
#define GNUMERIC_EXPR_H

#include "gnumeric.h"
#include "numbers.h"
#include "parse-util.h"
#include "position.h"

/* Warning: if you add something here, see do_expr_tree_to_string * ! */
typedef enum {
	OPER_EQUAL,		/* Compare value equal */
	OPER_GT,		/* Compare value greather than  */
	OPER_LT,		/* Compare value less than */
	OPER_GTE,		/* Compare value greather or equal than */
	OPER_LTE,		/* Compare value less or equal than */
	OPER_NOT_EQUAL,		/* Compare for non equivalence */

	OPER_ADD,		/* Add  */
	OPER_SUB,		/* Subtract */
	OPER_MULT,		/* Multiply */
	OPER_DIV,		/* Divide */
	OPER_EXP,		/* Exponentiate */
	OPER_CONCAT,		/* String concatenation */

	OPER_FUNCALL,		/* Function call invocation */

	OPER_NAME,              /* Name reference */

        OPER_CONSTANT,		/* Constant value */
	OPER_VAR,		/* Cell content lookup (variable) */
	OPER_UNARY_NEG,		/* Sign inversion */
	OPER_UNARY_PLUS,	/* Mark as positive */
	OPER_PERCENT,		/* Percentage (value/100) */
	OPER_ARRAY,		/* Array access */
	OPER_SET		/* A set of expressions */
} Operation;

/* Shorthands for case statements.  Easy to read, easy to maintain.  */
#define OPER_ANY_BINARY OPER_EQUAL: case OPER_GT: case OPER_LT: case OPER_GTE: \
	case OPER_LTE: case OPER_NOT_EQUAL: \
	case OPER_ADD: case OPER_SUB: case OPER_MULT: case OPER_DIV: \
	case OPER_EXP: case OPER_CONCAT
#define OPER_ANY_UNARY OPER_UNARY_NEG: case OPER_UNARY_PLUS : case OPER_PERCENT

struct _ExprConstant {
	Operation const oper;
	int       ref_count;

	Value  *value;
};

struct _ExprFunction {
	Operation const oper;
	int       ref_count;

	FunctionDefinition *func;
	ExprList  *arg_list;
};

struct _ExprUnary {
	Operation const oper;
	int       ref_count;

	ExprTree *value;
};

struct _ExprBinary {
	Operation const oper;
	int       ref_count;

	ExprTree *value_a;
	ExprTree *value_b;
};

/* We could break this out into multiple types to be more space efficient */
struct _ExprName {
	Operation const oper;
	int       ref_count;

	Sheet	 *optional_scope;
	Workbook *optional_wb_scope;
	NamedExpression *name;
};

struct _ExprVar {
	Operation const oper;
	int       ref_count;

	CellRef ref;
};

struct _ExprArray {
	Operation const oper;
	int       ref_count;

	int   x, y;
	int   cols, rows;
	struct {
		/* Upper left corner */
		Value *value;	/* Last array result */
		ExprTree *expr;	/* Real Expression */
	} corner;
};

struct _ExprSet {
	Operation const oper;
	int       ref_count;

	ExprList *set;
};

union _ExprTree {
	struct {
		Operation const oper;
		int       ref_count;
	} any;

	ExprConstant	constant;
	ExprFunction	func;
	ExprUnary	unary;
	ExprBinary	binary;
	ExprName	name;
	ExprVar		var;
	ExprArray	array;
	ExprSet		set;
};

#define expr_parse_str_simple(expr_text, pp) \
	expr_parse_str (expr_text, pp, GNM_PARSER_DEFAULT, NULL)
ExprTree   *expr_parse_str	   (char const *expr, ParsePos const *pp,
				    GnmExprParserFlags flags,
				    ParseError *error);
ExprTree   *expr_tree_duplicate    (ExprTree *expr);
char       *expr_tree_as_string    (ExprTree const *tree, ParsePos const *fp);

ExprTree   *expr_tree_new_constant (Value *v);
ExprTree   *expr_tree_new_error    (char const *txt);
ExprTree   *expr_tree_new_unary    (Operation op, ExprTree *e);
ExprTree   *expr_tree_new_binary   (ExprTree *l, Operation op, ExprTree *r);
ExprTree   *expr_tree_new_funcall  (FunctionDefinition *func, ExprList *args);
ExprTree   *expr_tree_new_name     (NamedExpression *name,
				    Sheet *sheet_scope, Workbook *wb_scope);
ExprTree   *expr_tree_new_var      (CellRef const *cr);
ExprTree   *expr_tree_new_array	   (int x, int y, int cols, int rows);
ExprTree   *expr_tree_new_set	   (ExprList *args);

void	    expr_tree_ref	(ExprTree *tree);
void	    expr_tree_unref	(ExprTree *tree);
gboolean    expr_tree_is_shared (ExprTree const *tree);
gboolean    expr_tree_equal	(ExprTree const *a, ExprTree const *b);

struct _ExprRelocateInfo {
	EvalPos pos;

	Range   origin;	        /* References to cells in origin_sheet!range */
	Sheet  *origin_sheet;	/* should to adjusted */

	Sheet  *target_sheet;	/* to point at this sheet */
	int col_offset, row_offset;/* and offset by this amount */
};

struct _ExprRewriteInfo {
	enum { EXPR_REWRITE_SHEET,
	       EXPR_REWRITE_WORKBOOK,
	       EXPR_REWRITE_RELOCATE } type;
	union {
		Sheet const     *sheet;
		Workbook const  *workbook;
		ExprRelocateInfo relocate;
	} u;
};

ExprTree       *expr_rewrite (ExprTree        const *expr,
			      ExprRewriteInfo const *rwinfo);

/*
 * Returns int(0) if the expression uses a non-existant cell for anything
 * other than an equality test.
 */
typedef enum
{
    EVAL_STRICT = 0x0,
    EVAL_PERMIT_NON_SCALAR = 0x1,
    EVAL_PERMIT_EMPTY = 0x2
} ExprEvalFlags;

Value       *expr_eval (ExprTree const *tree,
			EvalPos const *pos, ExprEvalFlags flags);

Value	    *expr_array_intersection (Value *v);
Value       *expr_implicit_intersection (EvalPos const *pos,
					 Value *v);

FunctionDefinition *expr_tree_get_func_def (ExprTree const *expr);
Value		   *expr_tree_get_range    (ExprTree const *expr) ;
ExprTree const	   *expr_tree_first_func   (ExprTree const *expr);
void		    expr_tree_boundingbox  (ExprTree const *expr, Range *bound);
GSList		   *expr_tree_referenced_sheets (ExprTree const *expr);

#define expr_list_append	g_slist_append
#define expr_list_prepend	g_slist_prepend
#define expr_list_length	g_slist_length
#define expr_list_free		g_slist_free
void 	 expr_list_unref	(ExprList *list);
char    *expr_list_as_string	(ExprList const *list, ParsePos const *pp);
gboolean expr_list_equal	(ExprList const *a, ExprList const *b);

#endif /* GNUMERIC_EXPR_H */

#ifndef GNUMERIC_EXPR_H
#define GNUMERIC_EXPR_H

#include "gnumeric.h"
#include "symbol.h"
#include "numbers.h"

/* Warning: if you add something here, see do_expr_decode_tree ! */
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
	OPER_ARRAY		/* Array access */
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

	Symbol *symbol;
	GList  *arg_list;
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

struct _ExprName {
	Operation const oper;
	int       ref_count;

	NamedExpression const *name;
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
	int   rows, cols;
	union {
		/* Upper left corner */
		struct {
			Value *value;	/* Last array result */
			ExprTree *expr;	/* Real Expression */
		} func;

		/* Others refer to corner cell directly */
		Cell *cell;
	} corner;
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
};

/**
 * Function parameter structures
 */
enum _FuncType { FUNCTION_ARGS, FUNCTION_NODES };
typedef enum _FuncType FuncType;

typedef Value *(FunctionArgs)  (FunctionEvalInfo *ei, Value **args);
typedef Value *(FunctionNodes) (FunctionEvalInfo *ei, GList *nodes);

struct _FunctionEvalInfo {
	EvalPosition const *pos;
	FunctionDefinition const *func_def;
};

/**
 * Used for getting a valid Sheet *from a CellRef
 * Syntax is CellRef, valid Sheet *
 */
#define eval_sheet(a,b)     (a?a:b)

/* Transition functions */
EvalPosition     *eval_pos_init       (EvalPosition *pp, Sheet *s, CellPos const *pos);
EvalPosition     *eval_pos_cell       (EvalPosition *pp, Cell const *cell);
EvalPosition     *eval_pos_cellref    (EvalPosition *dest,
				       EvalPosition const *src, CellRef const *);
ParsePosition    *parse_pos_init      (ParsePosition *pp, Workbook *wb, Sheet *sheet, int col, int row);
ParsePosition    *parse_pos_cell      (ParsePosition *pp, Cell const *cell);
ParsePosition    *parse_pos_evalpos   (ParsePosition *pp, EvalPosition const *pos);

/*
 * Built in / definable sheet names.
 */
struct _NamedExpression {
	String       *name;
	Workbook     *wb;
	Sheet        *sheet;
	gboolean      builtin;
	union {
		ExprTree     *expr_tree;
		FunctionArgs *expr_func;
	} t;
};

void        cell_ref_make_abs      (CellRef *dest,
				    CellRef const *src,
				    EvalPosition const *ep);
int         cell_ref_get_abs_col   (CellRef const *ref,
				    EvalPosition const *pos);
int         cell_ref_get_abs_row   (CellRef const *cell_ref,
				    EvalPosition const *src_fp);
void        cell_get_abs_col_row   (CellRef const *cell_ref,
				    CellPos const *pos,
				    int *col, int *row);

ExprTree   *expr_parse_string      (char const *expr, ParsePosition const *pp,
				    char **desired_format, char **error_msg);
ExprTree   *expr_tree_duplicate    (ExprTree *expr);
char       *expr_decode_tree       (ExprTree *tree, ParsePosition const *fp);

ExprTree   *expr_tree_new_constant (Value *v);
ExprTree   *expr_tree_new_error    (char const *txt);
ExprTree   *expr_tree_new_unary    (Operation op, ExprTree *e);
ExprTree   *expr_tree_new_binary   (ExprTree *l, Operation op, ExprTree *r);
ExprTree   *expr_tree_new_funcall  (Symbol *sym, GList *args);
ExprTree   *expr_tree_new_name     (NamedExpression const *name);
ExprTree   *expr_tree_new_var      (CellRef const *cr);
ExprTree   *expr_tree_new_array	   (int x, int y, int rows, int cols);

void        expr_tree_ref          (ExprTree *tree);
void        expr_tree_unref        (ExprTree *tree);

struct _ExprRelocateInfo {
	Range origin;		/* References to cells in origin_sheet!range */
	Sheet *origin_sheet;	/* should to adjusted */

	Sheet *target_sheet;	/* to point at this sheet */
	int col_offset, row_offset;/* and offset by this amount */
};

ExprTree       *expr_relocate (ExprTree const *expr,
			       EvalPosition const *pos,
			       ExprRelocateInfo const *info);

int             expr_tree_get_const_int (ExprTree const *expr);
char const *	expr_tree_get_const_str (ExprTree const *expr);
 
/* Debugging */ 
void expr_dump_tree (ExprTree const *tree);

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

Value       *eval_expr (EvalPosition const *pos,
			ExprTree const *tree,
			ExprEvalFlags flags);

Value       *expr_implicit_intersection (EvalPosition const *pos,
					 Value *v);

/* Setup of the symbol table */
void         functions_init        (void);
void         constants_init        (void);

#endif /* GNUMERIC_EXPR_H */

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

struct _ArrayRef {
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

struct _ExprTree {
	Operation oper;

	int       ref_count;
	union {
		Value  *constant;

		struct {
			Symbol *symbol;
			GList  *arg_list;
		} function;

		struct {
			ExprTree *value_a;
			ExprTree *value_b;
		} binary;

		const ExprName *name;
		
		ExprTree *value;

		CellRef ref;

		ArrayRef array;
	} u;
};

typedef enum {
	PARSE_OK,
	PARSE_ERR_NO_QUOTE,
	PARSE_ERR_SYNTAX,
	PARSE_ERR_UNKNOWN
} ParseErr;


/*
 * Function parameter structures
 */

enum _FuncType { FUNCTION_ARGS, FUNCTION_NODES };
typedef enum _FuncType FuncType;

typedef Value *(FunctionArgs)  (FunctionEvalInfo *ei, Value **args);
typedef Value *(FunctionNodes) (FunctionEvalInfo *ei, GList *nodes);

struct _FunctionEvalInfo {
	EvalPosition const * pos;
	FunctionDefinition const *func_def;
};

/*
 * Used for getting a valid Sheet * from a CellRef
 * Syntax is CellRef, valid Sheet *
 */
#define eval_sheet(a,b)     (a?a:b)

/* Transition functions */
EvalPosition     *eval_pos_init       (EvalPosition *, Sheet *s, int col, int row);
EvalPosition     *eval_pos_cell       (EvalPosition *, Cell *);
ParsePosition    *parse_pos_init      (ParsePosition *, Workbook *wb, int col, int row);
ParsePosition    *parse_pos_cell      (ParsePosition *, Cell *);

/*
 * Built in / definable sheet names.
 */
struct _ExprName {
	String       *name;
	Workbook     *wb;
	Sheet        *sheet;
	gboolean      builtin;
	union {
		ExprTree     *expr_tree;
		FunctionArgs *expr_func;
	} t;
};

void        cell_ref_make_abs      (CellRef * const dest,
				    CellRef const * const src,
				    EvalPosition const * const ep);
int         cell_ref_get_abs_col   (CellRef const * const ref,
				    EvalPosition const * const pos);
int         cell_ref_get_abs_row   (CellRef const * const cell_ref,
				    EvalPosition const * const src_fp);
void        cell_get_abs_col_row   (CellRef const * const cell_ref,
				    CellPos const * const pos,
				    int * const col, int * const row);

ExprTree   *expr_parse_string      (const char *expr, const ParsePosition *pp,
				    char **desired_format, char **error_msg);
/* In parser.y  */
ParseErr    gnumeric_expr_parser   (const char *expr,
				    const ParsePosition *pp,
				    char **desired_format,
				    ExprTree **result);

ExprTree   *expr_tree_duplicate    (ExprTree *expr);
char       *expr_decode_tree       (ExprTree *tree, const ParsePosition *fp);

ExprTree   *expr_tree_new_constant (Value *v);
ExprTree   *expr_tree_new_unary    (Operation op, ExprTree *e);
ExprTree   *expr_tree_new_binary   (ExprTree *l, Operation op, ExprTree *r);
ExprTree   *expr_tree_new_funcall  (Symbol *sym, GList *args);
ExprTree   *expr_tree_new_name     (const ExprName *name);
ExprTree   *expr_tree_new_var      (const CellRef *cr);
ExprTree   *expr_tree_new_error    (const char *txt);
ExprTree   *expr_tree_array_formula (int const x, int const y, int const rows,
				     int const cols);

void        expr_tree_ref          (ExprTree *tree);
void        expr_tree_unref        (ExprTree *tree);

struct _ExprRelocateInfo {
	Range origin;		/* References to cells in origin_sheet!range */
	Sheet *origin_sheet;	/* should to adjusted */

	Sheet *target_sheet;	/* to point at this sheet */
	int col_offset, row_offset;/* and offset by this amount */
};

ExprTree       *expr_relocate (const ExprTree *expr,
			       const EvalPosition *pos,
			       const ExprRelocateInfo *info);

int             expr_tree_get_const_int (ExprTree const *const expr);
char const *	expr_tree_get_const_str (ExprTree const *const expr);
 
/* Debugging */ 
void expr_dump_tree (const ExprTree *tree);

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

Value       *eval_expr (EvalPosition const * const pos,
			ExprTree const * const tree,
			ExprEvalFlags const flags);

Value       *expr_implicit_intersection (EvalPosition const * const pos,
					 Value * const v);

/* Setup of the symbol table */
void         functions_init        (void);
void         constants_init        (void);

#endif /* GNUMERIC_EXPR_H */

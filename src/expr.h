#ifndef GNUMERIC_EXPR_H
#define GNUMERIC_EXPR_H

/* Forward references for structures.  */
typedef struct _Value Value;
typedef struct _ExprTree ExprTree;
typedef struct _CellRef CellRef;

#include "sheet.h"
#include "symbol.h"
#include "numbers.h"
#include "str.h"

/* Some utility constants to make sure we all spell correctly */
/* TODO : These should really be const, but can't until error_string changes */
extern char *gnumeric_err_NULL;
extern char *gnumeric_err_DIV0;
extern char *gnumeric_err_VALUE;
extern char *gnumeric_err_REF;
extern char *gnumeric_err_NAME;
extern char *gnumeric_err_NUM;
extern char *gnumeric_err_NA;

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

        OPER_CONSTANT,		/* Constant value */
	OPER_VAR,		/* Cell content lookup (variable) */
	OPER_NEG,		/* Sign inversion */
} Operation;

/* Shorthands for case statements.  Easy to read, easy to maintain.  */
#define OPER_ANY_BINARY OPER_EQUAL: case OPER_GT: case OPER_LT: case OPER_GTE: \
	case OPER_LTE: case OPER_NOT_EQUAL: \
	case OPER_ADD: case OPER_SUB: case OPER_MULT: case OPER_DIV: \
	case OPER_EXP: case OPER_CONCAT
#define OPER_ANY_UNARY OPER_NEG

typedef enum {
	VALUE_STRING,
	VALUE_INTEGER,
	VALUE_FLOAT,
	VALUE_CELLRANGE,
	VALUE_ARRAY,
} ValueType;

struct _CellRef {
	Sheet *sheet;
	int   col, row;

	unsigned char col_relative;
	unsigned char row_relative;
};

struct _Value {
	ValueType type;
	union {
		CellRef cell;
		struct {
			CellRef cell_a;
			CellRef cell_b;
		} cell_range;

		struct {
			int x, y ;
			Value ***vals;  /* Array [x][y] */
		} array ;
		String *str;
		Symbol *sym;
		float_t v_float;	/* floating point */
		int_t   v_int;
	} v;
};

#define VALUE_IS_NUMBER(x) (((x)->type == VALUE_INTEGER) || \
			    ((x)->type == VALUE_FLOAT))

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

		ExprTree *value;

		CellRef ref;
	} u;
};

typedef enum {
	PARSE_OK,
	PARSE_ERR_NO_QUOTE,
	PARSE_ERR_SYNTAX
} ParseErr;

/*
 * Functions come in two fashions:  Those that only deal with
 * very specific data types and a constant number of arguments,
 * and those who don't.
 *
 * The former kind of functions receives a precomputed array of
 * Value pointers.
 *
 * The latter sort of functions receives the plain ExprNodes and
 * it is up to that routine to do the value computations and range
 * processing.
 */

struct FunctionDefinition {
	/* The function name */
	char  *name;

	/**
	 *  The types accepted: see writing-functions.smgl ( bottom )
	 * f for float
	 * s for string
	 * b for boolean
	 * r for cell range
	 * a for cell array
	 * A for 'area': either range or array
	 * ? for any kind
	 *  For optional arguments do:
	 * "ff|ss" where the strings are optional
	 **/
	char  *args;
	char  *named_arguments;
	char  **help;
	Value *(*expr_fn)(Sheet *sheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string);
	
	Value *(*fn)(struct FunctionDefinition *func_def, Value *argv [], char **error_string);
};

typedef struct FunctionDefinition FunctionDefinition;

void        cell_get_abs_col_row (const CellRef *cell_ref,
				  int eval_col, int eval_row,
				  int *col, int *row);

ExprTree   *expr_parse_string    (const char *expr, Sheet *sheet, int col, int row,
				  const char **desired_format, char **error_msg);
/* In parser.y  */
ParseErr    gnumeric_expr_parser (const char *expr, Sheet *sheet,
				  int col, int row, const char **desired_format,
				  ExprTree **tree);

ExprTree   *expr_tree_duplicate  (ExprTree *expr);
char       *expr_decode_tree     (ExprTree *tree, Sheet *sheet,
				  int col, int row);

ExprTree   *expr_tree_new_constant(Value *v);
ExprTree   *expr_tree_new_unary  (Operation op, ExprTree *e);
ExprTree   *expr_tree_new_binary (ExprTree *l, Operation op, ExprTree *r);
ExprTree   *expr_tree_new_funcall(Symbol *sym, GList *args);
ExprTree   *expr_tree_new_var    (const CellRef *cr);
ExprTree   *expr_tree_new_error  (const char *txt);

void        expr_tree_ref        (ExprTree *tree);
void        expr_tree_unref      (ExprTree *tree);

ExprTree   *expr_tree_invalidate_references (ExprTree *src, Sheet *src_sheet,
					     int src_col, int src_row, Sheet *sheet, int col, int row,
					     int colcount, int rowcount);

ExprTree   *expr_tree_fixup_references (ExprTree *src, Sheet *src_sheet,
					int src_col, int src_row, Sheet *sheet, int col, int row,
					int coldelta, int rowdelta);

Value      *eval_expr            (Sheet *sheet, ExprTree *tree,
				  int  col, int row,
				  char **error_string);

extern Value *value_zero;
Value       *value_new_float       (float_t f);
Value       *value_new_int         (int i);
Value       *value_new_bool        (gboolean b);
Value       *value_new_string      (const char *str);
Value       *value_new_cellrange   (const CellRef *a, const CellRef *b);

void         value_release         (Value *value);
Value       *value_duplicate       (const Value *value);
void         value_copy_to         (Value *dest, const Value *source);
Value       *value_cast_to_float   (Value *v);

int          value_get_as_bool     (const Value *v, int *err);
float_t      value_get_as_float    (const Value *v);
int          value_get_as_int      (const Value *v);
char        *value_get_as_string   (const Value *value);

void         value_dump            (const Value *value);

/* Area functions ( works on VALUE_RANGE or VALUE_ARRAY */
guint        value_area_get_width  (Value *v);
guint        value_area_get_height (Value *v);
const Value *value_area_get_at_x_y (Value *v, guint x, guint y);

Value       *value_array_new       (guint width, guint height);
void         value_array_set       (Value *array, guint col, guint row, Value *v);
void         value_array_resize    (Value *v, guint width, guint height);
void         value_array_copy_to   (Value *dest, const Value *src);

/* Setup of the symbol table */
void         functions_init        (void);
void         constants_init        (void);

#endif /* GNUMERIC_EXPR_H */

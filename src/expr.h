#ifndef GNUMERIC_EXPR_H
#define GNUMERIC_EXPR_H

/* Forward references for structures.  */
typedef struct _Value    Value;
typedef struct _ExprTree ExprTree;
typedef struct _CellRef  CellRef;
typedef struct _ArrayRef ArrayRef;
typedef struct _ExprName ExprName;

typedef struct _EvalPosition            EvalPosition;
typedef struct _ErrorMessage            ErrorMessage;
typedef struct _FunctionEvalInfo        FunctionEvalInfo;

#include "sheet.h"
#include "symbol.h"
#include "numbers.h"
#include "str.h"
#include "expr-name.h"

/* Some utility constants to make sure we all spell correctly */
/* TODO : These should really be const, but can't until error_string changes */
extern char *gnumeric_err_NULL;
extern char *gnumeric_err_DIV0;
extern char *gnumeric_err_VALUE;
extern char *gnumeric_err_REF;
extern char *gnumeric_err_NAME;
extern char *gnumeric_err_NUM;
extern char *gnumeric_err_NA;

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
	OPER_NEG,		/* Sign inversion */
	OPER_ARRAY		/* Array access */
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

/* FIXME: Should be tastefuly concealed */
typedef struct _FunctionDefinition FunctionDefinition;

struct _EvalPosition {
	Sheet *sheet;
	int    eval_col;
	int    eval_row;
};

enum _FuncType { FUNCTION_ARGS, FUNCTION_NODES };
typedef enum _FuncType FuncType;

typedef Value *(FunctionArgs)  (FunctionEvalInfo *ei, Value **args);
typedef Value *(FunctionNodes) (FunctionEvalInfo *ei, GList *nodes);

struct _ErrorMessage {
	const char *message;
	char       *alloc;
	char        small [20];
};

ErrorMessage *error_message_new       (void);
void          error_message_set       (ErrorMessage *em, const char *message);
gboolean      error_message_is_set    (ErrorMessage *em);
const char   *error_message_txt       (ErrorMessage *em);
void          error_message_free      (ErrorMessage *em);

struct _FunctionEvalInfo {
	EvalPosition        pos;
	ErrorMessage       *error;
	FunctionDefinition *func_def;
};

/*
 * Used for getting a valid Sheet * from a CellRef
 * Syntax is CellRef, valid Sheet *
 */
#define eval_sheet(a,b)     (a?a:b)

Value *function_error       (FunctionEvalInfo *fe,
			     char *error_string);
Value *function_error_alloc (FunctionEvalInfo *fe,
			     char *error_string);

/* Transition functions */
EvalPosition     *eval_pos_init       (EvalPosition *, Sheet *s, int col, int row);
EvalPosition     *eval_pos_cell       (EvalPosition *, Cell *);
FunctionEvalInfo *func_eval_info_init (FunctionEvalInfo *s, Sheet *sheet, int col, int row);
FunctionEvalInfo *func_eval_info_cell (FunctionEvalInfo *s, Cell *cell);
FunctionEvalInfo *func_eval_info_pos  (FunctionEvalInfo *s, const EvalPosition *fp);

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

struct _FunctionDefinition {
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
	FuncType fn_type;
	union {
		FunctionNodes *fn_nodes;
		FunctionArgs  *fn_args;
	} fn;
};

/*
 * Built in / definable sheet names.
 */
struct _ExprName {
	String       *name;
	Workbook     *wb;
	gboolean      builtin;
	union {
		ExprTree     *expr_tree;
		FunctionArgs *expr_func;
	} t;
};

void        cell_get_abs_col_row   (const CellRef *cell_ref,
				    int eval_col, int eval_row,
				    int *col, int *row);

ExprTree   *expr_parse_string      (const char *expr, const EvalPosition *fp,
				    const char **desired_format, char **error_msg);
/* In parser.y  */
ParseErr    gnumeric_expr_parser   (const char *expr, const EvalPosition *ep,
				    const char **desired_format,
				    ExprTree **result);

ExprTree   *expr_tree_duplicate    (ExprTree *expr);
char       *expr_decode_tree       (ExprTree *tree, const EvalPosition *fp);

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

ExprTree   *expr_tree_invalidate_references (ExprTree *src, EvalPosition *src_fp,
					     const EvalPosition *fp,
					     int colcount, int rowcount);

ExprTree   *expr_tree_fixup_references (ExprTree *src, EvalPosition *src_fp,
					const EvalPosition *fp,
					int coldelta, int rowdelta);

int             expr_tree_get_const_int (ExprTree const *const expr);
char const *	expr_tree_get_const_str (ExprTree const *const expr);
 
/* Debugging */ 
void expr_dump_tree (ExprTree *tree);

/*
 * Returns int(0) if the expression uses a non-existant cell for anything
 * other than an equality test, returns NULL on error (for now)
 */
Value       *eval_expr             (FunctionEvalInfo *s, ExprTree *tree);

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
char        *value_cellrange_get_as_string
                                   (const Value *value,
				    gboolean use_relative_syntax);

void         value_dump            (const Value *value);

/* Area functions ( works on VALUE_RANGE or VALUE_ARRAY */
/* The EvalPosition provides a Sheet context; this allows
   calculation of relative references. 'x','y' give the position */
guint        value_area_get_width  (const EvalPosition *ep, Value const *v);
guint        value_area_get_height (const EvalPosition *ep, Value const *v);

/* Return Value(int 0) if non-existant */
const Value *value_area_fetch_x_y  (const EvalPosition *ep, Value const * v,
				    guint x, guint y);

/* Return NULL if non-existant */
const Value * value_area_get_x_y (const EvalPosition *ep, Value const * v,
				  guint x, guint y);

Value       *value_array_new       (guint width, guint height);
void         value_array_set       (Value *array, guint col, guint row, Value *v);
void         value_array_resize    (Value *v, guint width, guint height);
void         value_array_copy_to   (Value *dest, const Value *src);

/* Setup of the symbol table */
void         functions_init        (void);
void         constants_init        (void);

#endif /* GNUMERIC_EXPR_H */

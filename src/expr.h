#ifndef GNUMERIC_EXPR_H
#define GNUMERIC_EXPR_H

typedef enum {
	OPER_EQUAL,		/* Compare value equal */
	OPER_GT,		/* Compare value greather than  */
	OPER_LT,		/* Compare value less than */
	OPER_GTE,		/* Compare value greather or equal than */
	OPER_LTE,		/* Compare value less or equal than */
	OPER_NOT_EQUAL,		/* Compare for non equivalence */
	
	OPER_ADD,		/* Add  */
	OPER_SUB,		/* Substract */
	OPER_MULT,		/* Multiply */
	OPER_DIV,		/* Divide */
	OPER_EXP,		/* Exponentiate */
	OPER_CONCAT,		/* String concatenation */

	OPER_FUNCALL,		/* Function call invocation */

        OPER_CONSTANT,		/* Constant value */
	OPER_VAR,		/* Cell content lookup (variable) */
	OPER_NEG,		/* Sign inversion */
} Operation;

typedef enum {
	VALUE_STRING,
	VALUE_INTEGER,
	VALUE_FLOAT,
	VALUE_CELLRANGE,
	VALUE_ARRAY,
} ValueType;

typedef struct {
	void *sheet;
	int   col, row;

	unsigned int col_relative:1;
	unsigned int row_relative:1;
} CellRef;

typedef struct {
	ValueType type;
	union {
		CellRef cell;
		struct {
			CellRef cell_a;
			CellRef cell_b;
		} cell_range;

		GList  *array;	        /* a list of Values */
		String *str;
		Symbol *sym;
		float_t v_float;	/* floating point */
		int_t   v_int;
	} v;
} Value;

#define VALUE_IS_NUMBER(x) (((x)->type == VALUE_INTEGER) || \
			    ((x)->type == VALUE_FLOAT))

struct ExprTree {
	Operation oper;

	int       ref_count;
	union {
		Value  *constant;

		struct {
			Symbol *symbol;
			GList  *arg_list;
		} function;
		
		struct {
			struct ExprTree *value_a;
			struct ExprTree *value_b;
		} binary;

		struct ExprTree *value;

		CellRef ref;
	} u;
};

typedef struct ExprTree ExprTree;

typedef enum {
	PARSE_OK,
	PARSE_ERR_NO_QUOTE,
	PARSE_ERR_SYNTAX
} ParseErr;

/*
 * Functions come in two fashions:  Those that only deal with
 * very specific data types and a constant number of arguments,
 * and those who dont.
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

	/* The types accepted:
	 * f for float
	 * s for string
	 * b for boolean
	 * ? for any kind
	 */
	char  *args;
	char  *named_arguments;
	char  **help;
	Value *(*expr_fn)(void *sheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string);
	
	Value *(*fn)(struct FunctionDefinition *func_def, Value *argv [], char **error_string);
};

typedef struct FunctionDefinition FunctionDefinition;

/* For communication with yyparse */
extern char     *parser_expr;
extern char     *parser_desired_format;
extern ParseErr  parser_error;
extern ExprTree *parser_result;
extern void     *parser_sheet;
extern int       parser_col, parser_row;

void        cell_get_abs_col_row (CellRef *cell_ref,
				  int eval_col, int eval_row,
				  int *col, int *row);

ExprTree   *expr_parse_string    (char *expr, void *sheet, int col, int row,
				  char **desired_format, char **error_msg);
ExprTree   *expr_tree_duplicate  (ExprTree *expr);
ExprTree   *expr_tree_relocate   (ExprTree *expr, int col_diff, int row_diff);
char       *expr_decode_tree     (ExprTree *tree, void *sheet,
				  int col, int row);

void        expr_tree_ref        (ExprTree *tree);
void        expr_tree_unref      (ExprTree *tree);

Value      *eval_expr            (void *asheet, ExprTree *tree,
				  int  col, int row,
				  char **error_string);

void        value_release        (Value *value);
Value      *value_cast_to_float  (Value *v);
int         value_get_bool       (Value *v, int *err);
float_t     value_get_as_double  (Value *v);
int         value_get_as_int     (Value *v);
void        value_copy_to        (Value *dest, Value *source);
				 
void        value_dump           (Value *value);
char       *value_string         (Value *value);
Value      *value_duplicate      (Value *value);

Value      *value_float          (float_t f);
Value      *value_int            (int i);
Value      *value_str            (char *str);
	
int         yyparse              (void);

/* Setup of the symbol table */
void        functions_init       (void);
void        constants_init       (void);

#endif /* GNUMERIC_EXPR_H */

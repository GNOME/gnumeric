#ifndef EXPR_H
#define EXPR_H

typedef enum {
	OP_EQUAL,
	OP_GT,
	OP_LT,
	OP_GTE,
	OP_LTE,
	OP_NOT_EQUAL,
	
	OP_ADD,
	OP_SUB,
	OP_MULT,
	OP_DIV,
	OP_EXP,
	OP_CONCAT,

	OP_FUNCALL,

        OP_CONSTANT,
	OP_VAR,
	OP_NEG
} Operation;

typedef enum {
	VALUE_STRING,
	VALUE_INTEGER,
	VALUE_FLOAT,
	VALUE_CELLRANGE,
	VALUE_ARRAY
} ValueType;

/*
 * We use the GNU Multi-precission library for storing our 
 * numbers
 */
typedef struct {
	int col;
	int row;
} CellPos;

typedef struct {
	int col, row;

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

	/*
	 * The reference count is only used by the toplevel tree.
	 */
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
typedef struct {
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
	
	Value *(*fn)(Value *argv [], char **error_string);
} FunctionDefinition;

/* For communication with yyparse */
extern char     *parser_expr;
extern ParseErr  parser_error;
extern ExprTree *parser_result;
extern int       parser_col, parser_row;

void        cell_get_abs_col_row (CellRef *cell_ref, int eval_col, int eval_row, int *col, int *row);

ExprTree   *expr_parse_string    (char *expr, int col, int row, char **error_msg);
void        expr_tree_ref        (ExprTree *tree);
void        expr_tree_unref      (ExprTree *tree);

/* Do not use this routine, it is intended to be used internally */
void        eval_expr_release    (ExprTree *tree);
				 
Value      *eval_expr            (void *asheet, ExprTree *tree,
				  int  col, int row,
				  char **error_string);
				 
void        value_release        (Value *value);
Value      *value_cast_to_float  (Value *v);
int         value_get_bool       (Value *v, int *err);
float_t     value_get_as_double  (Value *v);
void        value_copy_to        (Value *dest, Value *source);
				 
void        value_dump           (Value *value);
char       *value_string         (Value *value);
Value      *value_duplicate      (Value *value);

int         yyparse              (void);

/* Setup of the symbol table */
void        functions_init       (void);
void        constants_init       (void);

#endif

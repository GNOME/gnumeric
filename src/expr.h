#ifndef EXPR_H
#define EXPR_H

typedef enum {
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
	VALUE_CELLRANGE
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

/* For communication with yyparse */
extern char     *parser_expr;
extern ParseErr  parser_error;
extern ExprTree *parser_result;
extern int       parser_col, parser_row;

ExprTree   *expr_parse_string   (char *expr, int col, int row, char **error_msg);
void        expr_tree_ref       (ExprTree *tree);
void        expr_tree_unref     (ExprTree *tree);

Value      *eval_expr           (void *asheet, ExprTree *tree,
				 int  col, int row,
				 char **error_string);

void        value_release       (Value *value);
Value      *value_cast_to_float (Value *v);

void        value_dump          (Value *value);
char       *value_string        (Value *value);

int         yyparse             (void);

#endif

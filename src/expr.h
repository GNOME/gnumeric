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

	unsigned int col_abs:1;
	unsigned int row_abs:1;
} CellRef;

typedef struct {
	ValueType type;
	union {
		CellRef cell;
		struct {
			CellRef cell_a;
			CellRef cell_b;
		} cell_range;

		Symbol *str;
		float_t v_float;	/* floating point */
		int_t   v_int;
	} v;
} Value;

#define VALUE_IS_NUMBER(x) (((x)->type == VALUE_INTEGER) || \
			    ((x)->type == VALUE_FLOAT))

struct EvalNode {
	Operation oper;
	union {
		Value  *constant;

		struct {
			Symbol *symbol;
			GList  *arg_list;
		} function;
		
		struct {
			struct EvalNode *value_a;
			struct EvalNode *value_b;
		} binary;

		struct EvalNode *value;
	} u;
};

typedef struct EvalNode EvalNode;

typedef enum {
	PARSE_OK,
	PARSE_ERR_NO_QUOTE,
	PARSE_ERR_SYNTAX
} ParseErr;

/* For talking to yyparse */
extern char     *parser_expr;
extern ParseErr  parser_error;
extern EvalNode *parser_result;

EvalNode   *eval_parse_string  (char *expr, char **error_msg);
Value      *eval_node_value    (void *asheet, EvalNode *node,
				char **error_string);

void        eval_release_node  (EvalNode *node);
void        eval_release_value (Value *value);
Value      *eval_cast_to_float (Value *v);

void        eval_dump_value    (Value *value);
char       *eval_value_string  (Value *value);

int         yyparse            (void);

#endif

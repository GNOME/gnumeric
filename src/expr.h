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
	OP_CAST_TO_STRING,
	OP_NEG
} Operation;

typedef enum {
	VALUE_STRING,
	VALUE_NUMBER,
	VALUE_CELLRANGE
} ValueType;

/*
 * We use the GNU Multi-precission library for storing our 
 * numbers
 */
typedef struct {
	int col;
	int row;
} CellRef;

typedef struct {
	ValueType type;
	union {
		struct {
			CellRef a;
			CellRef b;
		} cell;

		Symbol *str;
		mpf_t number;	/* floating point */
	} v;
} Value;

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

/* For talking to yyparse */
extern char     *parser_expr;
extern char     **parser_error_message;
extern EvalNode *parser_result;

EvalNode   *eval_parse_string (char *expr, int col, int row, char **error_msg);
void        eval_release_node (EvalNode *node);
int         yyparse           (void);

#endif

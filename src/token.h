#ifndef TOKEN_H
#define TOKEN_H

typedef enum {
	R_NONE,
	R_INTEGER,
	R_STRING,
	R_STRING_STATIC,	/* Just an optimization */
	R_FLOAT
} token_tag_t;

typedef struct {
	token_tag_t  type;
	union {
		int  int_value;
		char *str_value;
		char str_value_small [20];
		double float_value;
	} v;
} token_result_t;

int  token_get_next (char **str, result_t *res);
void token_done (result_t *);

#endif

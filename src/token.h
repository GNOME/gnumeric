#ifndef TOKEN_H
#define TOKEN_H

typedef enum {
	TOKEN_IDENTIFIER = -1,
	TOKEN_NUMBER     = -2,
	TOKEN_ELLIPSIS   = -4,
	TOKEN_ERROR      = -5
} TokenType;

#define SMALL_STR_TOKEN 20

typedef enum {
	R_NONE,
	R_CHAR,
	R_INTEGER,
	R_STRING,
	R_FLOAT
} token_tag_t;

typedef struct {
	token_tag_t  type;
	union {
		mpz_t  int_value;
		char   *str_value;
		mpf_t  float_value;
	} v;
	char str_inline [SMALL_STR_TOKEN+1];
} token_result_t;

int    token_get_next (char **str, token_result_t *res);
void   token_done (token_result_t *res);

#endif

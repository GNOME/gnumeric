#include <gmp.h>
#include <glib.h>
#include <ctype.h>
#include "token.h"
#include "string.h"

#define CANSKIP(x) ((x) == ' ' || (x) == '\t')

int
token_get_next (char **str, token_result_t *res)
{
	char *p, *q, *start;
	int mp_res, len;
	
	g_return_val_if_fail (str != NULL, TOKEN_ERROR);
	g_return_val_if_fail (*str != NULL, TOKEN_ERROR);
	g_return_val_if_fail (res != NULL, TOKEN_ERROR);
	
	p = *str;
	
	while (*p && CANSKIP (*p))
		p++;

	if (*p == '.' && *(p+1) == '.'){
		*str = p+2;
		return TOKEN_ELLIPSIS;
	}
	if (*p == '\''|| isalpha (*p)){
		q = p+1;
		if (*p == '\''){
			start = p + 1;
			while (*q && *q != '\'')
				q++;
			q++;
		} else {
			start = p;
			while (*q && isalnum (*q))
				q++;
		}
		
		/* Store result */
		len = q - p;
		if (len < SMALL_STR_TOKEN)
			res->v.str_value = res->str_inline;
		else
			res->v.str_value = g_new (char, len + 1);

		res->type = R_STRING;
		
		strncpy (res->v.str_value, p, len);
		res->v.str_value [len] = 0;
		*str = q;
		return TOKEN_IDENTIFIER;
	}

	if (isdigit (*p)){
		int is_float = 0;
		char *copy;

		/* Scan for the whole number */
		q = p;
		while (isdigit (*q) || *q == '.'){
			if (*q == '.')
				is_float = 1;
			q++;
		}

		copy = alloca (q - p + 1);
		strncpy (copy, p, q - p);
		copy [q-p] = 0;

		/* Convert to MP number */
		if (is_float){
			res->type = R_FLOAT;
			mp_res = mpf_init_set_str (res->v.float_value, copy, 10);
		} else {
			res->type = R_INTEGER;
			mp_res = mpz_init_set_str (res->v.int_value, copy, 10);
		}
		*str = q;
		
		return TOKEN_NUMBER;
	}
	if (*p)
		*str = p + 1;
	else
		*str = p;

	res->type = R_CHAR;
	return *p;
}

/*
 * Should be invoked when a token has been used
 */
void
token_done (token_result_t *r)
{
	g_return_if_fail (r != NULL);
			  
	if (r->type == R_STRING){
		if (r->v.str_value != &r->str_inline)
			g_free (r->v.str_value);
	} else if (r->type == R_FLOAT)
		mpf_clear (r->v.float_value);
	else if (r->type == R_INTEGER)
		mpz_clear (r->v.int_value);

	r->type = R_NONE;
}
	    

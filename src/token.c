#include <glib.h>
#include <ctype.h>

#define CANSKIP(x) ((x) == ' ' || (x) == '\t')

int
token_get_next (char **str, result_t *res)
{
	char *p, *q;
	char *res;
	
	g_return_val_if_fail (str != NULL, TOKEN_ERROR);
	g_return_val_if_fail (*str != NULL, TOKEN_ERROR);
	g_return_val_if_fail (res != NULL, TOKEN_ERROR);
	
	p = *str;
	
	while (*p && CANSKIP (*p))
		*p++;

	if (*p == '\''){
		q = p+1;
		while (*q && *q != '\'')
			q++;
		if (*q){
			q++;

			/* Store result */
			res->v.str_val = g_new (char, p - q + 1);
			strncpy (res->v.str_val, p, p - q);
			res->v.str_val [p-q] = 0;
			res->type = R_STRING;
			
			*str = q;
			return TOKEN_IDENTIFIER;
		} else {
			return TOKEN_ERROR;
		}
	}

	if (isalpha (*p)){
		q = p+1;

		while (*q && isalnum (*q))
			q++;
		res->v.str_val = g_new (char, p - q + 1);
		strncpy (res->v.str_val, p, p - q);
		res->v.str_val [p-q] = 0;
		res->type = R_STRING;
		*str = q;
		return TOKEN_IDENTIFIER;
	}

	if (isnum (*p)){
		int is_float = 0;

		q = p;
		while (isnum (*q) || *q == '.'){
			if (q == '.')
				is_float = 1;
		}
		
}

/*
 * Should be invoked when a token has been used
 */
void
token_done (result_t *r)
{
	g_return_if_fail (r != NULL);
	g_return_if_fail (r->type != R_NONE;
			  
	if (r->type == R_STRING)
		g_free (r->v.str_value);
	r->type = R_NONE;
}
	    



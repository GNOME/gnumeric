#include <gnumeric-config.h>
#include <gnumeric.h>
#include <gmp.h>
#include <stdio.h>
#include <stdlib.h>
#include "token.h"

char *tests [] = {
	"5+4",
	"SUM(A1..A5)",
	"F(56.4+A)",
	NULL
};

int
main ()
{
	token_result_t res;
	char *s;
	int i, v;
	mp_exp_t exp;

	for (i = 0; tests [i]; i++){
		char *test = tests [i];
		int v;

		printf ("Expression: %s\n", test);

		while ((v = token_get_next (&test, &res)) != 0){
			switch (v){
			case TOKEN_IDENTIFIER:
				printf ("id=%s, ", res.v.str_value);
				break;

			case TOKEN_NUMBER:
				if (res.type == R_FLOAT){
					s = mpf_get_str (NULL, &exp, 0, 10, res.v.float_value);
					printf ("float=%c.%sE%d, ", s [0], &s[1], exp);
				} else {
					s = mpz_get_str (NULL, 10, res.v.int_value);
					printf ("int=%s, ", s);
				}
				free (s);
				break;

			case TOKEN_ELLIPSIS:
				printf ("(..)");
				break;

			case TOKEN_ERROR:
				printf ("ERROR!\n");

			default:
				printf ("char[%c] ", v);
			}
			token_done (&res);
			printf ("\n");
		}
		printf ("\n");
	}

	return 0;
}

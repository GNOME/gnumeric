#include <glib.h>
#include <stdio.h>
#include "numbers.h"
#include "symbol.h"
#include "expr.h"

char *exp [] = {
	"1+2",
	"1.5+1.2",
	"1.0*5.3",
	"2*4.1",
	"4.1*3",
	"5/0",
	"10.0/0",
	"a1+1",
	"$a1+1",
	"a$1+1",
	"$a$1+1",
	NULL
};

int
main ()
{
	Value *v;
	EvalNode *node;
	int i;
	char *error;
	
	for (i = 0; exp [i]; i++){
		printf ("Expression: %s;  ", exp [i]);
		node = eval_parse_string (exp [i], &error);
		if (node == NULL){
			printf ("parse error: %s\n", error);
			continue;
		}
		v = eval_node_value (NULL, node, &error);
		if (v == NULL){
			printf ("eval error: %s\n", error);
			continue;
		}
		eval_dump_value (v);
	}
	
	return 0;
}

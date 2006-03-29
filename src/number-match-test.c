#include <gnumeric-config.h>
#include "gnumeric.h"
#include "number-match.h"

#include <stdio.h>

int
main ()
{
	char buffer [100], *format;
	double v;
	char *s;

	/* format_match_init (); */

	/* To test the formats of the type "0.0 pesos" */
	format_match_define ("0.0 \"pesos\"");

	printf ("Enter a string that you want to match against the\n"
		"Gnumeric formats\n");

	while (1){
		printf ("> ");
		fflush (stdout);

		s = gets (buffer);
		if (!s)
			break;

		if (s [strlen (s)-1] == '\n')
			s [strlen (s)-1] = 0;

		if (format_match (s, NULL, &format))
			printf ("Format matched: %s\n", format);
		else
			printf ("No match found\n");
	}
	return 0;
}

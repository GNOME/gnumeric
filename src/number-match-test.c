#include <config.h>
#include <gnome.h>
#include "number-match.h"

int
main ()
{
	char buffer [100];
	char *s;
	
	format_match_init ();

	/* To test the formats of the type "0.0 pesos" */
	format_match_define ("0.0 \"pesos\"");

	while (1){
		printf ("> ");
		fflush (stdout);

		s = gets (buffer);
		if (!s)
			break;

		if (s [strlen (s)-1] == '\n')
			s [strlen (s)-1] = 0;
		
		format_match (s);
	}
	return 0;
}

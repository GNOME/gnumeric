#include <gnumeric-config.h>
#include <gnumeric.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "format.h"

void
test ()
{
	double timec = 36022.63582;

	printf( "%s|\n", format_number (12.0,  "0", NULL));
	printf( "%s|\n", format_number (12.3456789,  "??0000.00?", NULL));
	printf( "%s|\n", format_number (12.3,        "??0000.00?", NULL));
	printf( "%s|\n", format_number (12345.6789,  "??0000.00?", NULL));
	printf( "%s|\n", format_number (0.123456789, "???????.00", NULL));
	printf( "%s|\n", format_number (12200000,    "???0.000??#,,", NULL));
	printf( "%s|\n", format_number (timec, "hh:mm:ss", NULL));
	printf( "%s|\n", format_number (timec, "mmmm dd, yyyy", NULL));
	printf( "%s|\n", format_number (timec, "mmm d, yy h:m:s", NULL));
	printf( "%s|\n", format_number (timec, "mmm d, yy h:m:s PM", NULL));
	timec += 2 / 24.0;	       (
	printf( "%s|\n", format_number (timec, "hh:mm:ss", NULL));
	printf( "%s|\n", format_number (timec, "mmmm dd, yyyy", NULL));
	printf( "%s|\n", format_number (timec, "mmm d, yy h:m:s", NULL));
	printf( "%s|\n", format_number (timec, "mmm d, yy h:m:s PM", NULL));
}

int
main( int argc, gchar *argv[])
{
	bindtextdomain (PACKAGE, GNOMELOCALEDIR);
	textdomain (PACKAGE);
	test ();
	return 0;
}


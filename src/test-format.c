#include <config.h>
#include <glib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <gnome.h>
#include <time.h>

void
test()
{
  double timec = 36022.63582;
  
  printf( "%s|\n", format_number( 12.3456789,  "??0000.00?" ) );
  printf( "%s|\n", format_number( 12.3,        "??0000.00?" ) );
  printf( "%s|\n", format_number( 12345.6789,  "??0000.00?" ) );
  printf( "%s|\n", format_number( 0.123456789, "???????.00" ) );
  printf( "%s|\n", format_number( 12200000,    "???0.000??#,," ) );
  printf( "%s|\n", format_number( timec, "hh:mm:ss" ) );
  printf( "%s|\n", format_number( timec, "mmmm dd, yyyy" ) );
  printf( "%s|\n", format_number( timec, "mmm d, yy h:m:s" ) );
  printf( "%s|\n", format_number( timec, "mmm d, yy h:m:s PM" ) );
  timec += 2 / 24.0;
  printf( "%s|\n", format_number( timec, "hh:mm:ss" ) );
  printf( "%s|\n", format_number( timec, "mmmm dd, yyyy" ) );
  printf( "%s|\n", format_number( timec, "mmm d, yy h:m:s" ) );
  printf( "%s|\n", format_number( timec, "mmm d, yy h:m:s PM" ) );
}

int
main( int argc, gchar *argv[] )
{
  bindtextdomain(PACKAGE, GNOMELOCALEDIR);
  textdomain(PACKAGE);
  test();
  return 0;
}


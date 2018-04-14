#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&setup_python_environment ();

my $python_script = $0;
$python_script =~ s/\.pl$/.py/;
&test_command ($PYTHON . ' ' . &GnumericTest::quotearg ($python_script),
	       sub {
		   return (# A few important savers
			   /^Saver ID: Gnumeric_Excel:xlsx$/m &&
			   /^Saver ID: Gnumeric_stf:stf_csv$/m &&
			   /^Saver ID: Gnumeric_XmlIO:sax$/m &&
			   /^Saver ID: Gnumeric_pdf:pdf_assistant$/m &&
			   # A few important loaders
			   /^Loader ID: Gnumeric_Excel:xlsx$/m &&
			   /^Loader ID: Gnumeric_OpenCalc:openoffice$/m &&
			   # A few important plot types
			   /^Plot families: .*\bLine\b/m &&
			   /^Plot families: .*\bBar\b/m &&
			   /^Plot families: .*\bXY\b/m &&
			   # A few functions
			   /^Functions: .*\bsin\b/m &&
			   /^Functions: .*\bvar\b/m &&
			   /^Functions: .*\brandnorm\b/m &&
			   /^Functions: .*\bweekday\b/m &&
			   /^Functions: .*\bimsin\b/m
		       );
	       });

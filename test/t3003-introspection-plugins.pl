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
			   /^Loader ID: Gnumeric_OpenCalc:openoffice$/m);
	       });

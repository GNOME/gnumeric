#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&setup_python_environment ();

my $python_script = $0;
$python_script =~ s/\.pl$/.py/;
&test_command ($PYTHON . ' ' . &GnumericTest::quotearg ($python_script),
	       sub { /^Using in-tree / });

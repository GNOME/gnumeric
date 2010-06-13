#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $expected;
{ local $/; $expected = <DATA>; }

&message ("Checking function help text sanity.");
&sstest ("test_func_help", $expected);

__DATA__
-----------------------------------------------------------------------------
Start: test_func_help
-----------------------------------------------------------------------------

Result = 0
End: test_func_help

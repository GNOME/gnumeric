#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $expected;
{ local $/; $expected = <DATA>; }

&message ("Checking the text import parser.");
&sstest ("test_stf_parse", $expected);

__DATA__
-----------------------------------------------------------------------------
Start: test_stf_parse
-----------------------------------------------------------------------------

Result = 0
End: test_stf_parse

#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $expected;
{ local $/; $expected = <DATA>; }

&message ("Checking handling of non-ascii numbers.");
&sstest ("test_nonascii_numbers", $expected);

__DATA__
-----------------------------------------------------------------------------
Start: test_nonascii_numbers
-----------------------------------------------------------------------------

Result = 0
End: test_nonascii_numbers

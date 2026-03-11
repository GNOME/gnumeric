#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check for duplicate includes.");
my $checker = "tools/check-duplicate-includes.py";
&GnumericTest::report_skip ("Missing tester") unless -r "$topsrc/$checker";
&test_command ("cd $topsrc && $PERL $checker `find . -type f -name '*.[ch]' -print`",
	       sub { /^$/ });

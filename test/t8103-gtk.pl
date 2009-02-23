#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check that code includes gtk toplevel only.");
my $checker = "tools/check-gtk-includes";
&GnumericTest::report_skip ("Missing tester") unless -r "$topsrc/$checker";
&test_command ("cd $topsrc && $PERL $checker", sub { /^$/ });

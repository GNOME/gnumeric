#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check core glib slice checker");
$ENV{'G_SLICE'} = 'debug-blocks';

my $src = "$samples/regress.gnumeric";
&GnumericTest::report_skip ("file $src does not exist") unless -r $src;

my $tmp = "regress.xls";
&GnumericTest::junkfile ($tmp);

&test_command ("$ssconvert --recalc $src $tmp", sub { 1 } );

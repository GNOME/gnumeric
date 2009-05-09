#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check style optimizer.");
$ENV{'GNM_DEBUG'} = 'style-optimize:style-optimize-verify';

my $src = "$samples/excel/statfuns.xls";
&GnumericTest::report_skip ("file $src does not exist") unless -r $src;

my $tmp = "statfuns.gnumeric";
&GnumericTest::junkfile ($tmp);

&test_command ("$ssconvert $src $tmp", sub { 1 } );

#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check the ods exporter.");

my $src = "$samples/excel/finfuns.xls";
&GnumericTest::report_skip ("file $src does not exist") unless -r $src;

my $tmp = $src;
$tmp =~ s|^.*/||;
$tmp =~ s|\..*|.ods|;
&GnumericTest::junkfile ($tmp);
system ("$ssconvert $src $tmp");

&test_exporter ($tmp);

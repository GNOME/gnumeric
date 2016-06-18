#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check the csv importer.");

&test_csv_format_guessing
    (data => <<DATA, format => sub { /^d.*m.*y.*$/i; } );
"Date"
"1/2/3"
"1/2/2000"
"31/12/2000"
DATA

&test_csv_format_guessing
    (data => <<DATA, format => sub { /^m.*d.*y.*$/i; } );
"Date"
"1/2/3"
"1/2/2000"
"12/31/2000"
DATA

&test_csv_format_guessing
    (data => <<DATA, format => sub { /^y.*m.*d.*$/i; } );
"Date"
"2000-12-01"
DATA

print STDERR "Pass\n";

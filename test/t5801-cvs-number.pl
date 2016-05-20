#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check the csv importer.");

&test_csv_format_guessing
    (data => <<DATA, format => sub { return $_ eq '0.00'; }, decimal => '.' );
"Values"
"123.45"
"1.45"
DATA

&test_csv_format_guessing
    (data => <<DATA, format => sub { return $_ eq '0.00'; }, decimal => ',' );
"Values"
"123,45"
"1,45"
DATA

&test_csv_format_guessing
    (data => <<DATA, format => sub { return $_ eq 'General'; }, decimal => '.' );
"Values"
"123.456"
"1.45"
DATA

&test_csv_format_guessing
    (data => <<DATA, format => sub { return $_ eq 'General'; }, decimal => ',' );
"Values"
"123,456"
"1,45"
DATA

&test_csv_format_guessing
    (data => <<DATA, format => sub { return $_ eq 'General'; }, decimal => '.', thousand => ',' );
"Values"
"123,456"
"1.45"
DATA

&test_csv_format_guessing
    (data => <<DATA, format => sub { return $_ eq 'General'; }, decimal => ',', thousand => '.' );
"Values"
"123.456"
"1,45"
DATA

&test_csv_format_guessing
    (data => <<DATA, format => sub { return $_ eq 'General'; } );
"Values"
"123.456"
"234,567"
DATA

&test_csv_format_guessing
    (data => <<DATA, format => sub { return $_ eq '0.000'; }, decimal => ',' );
"Values"
"0,456"
DATA

&test_csv_format_guessing
    (data => <<DATA, format => sub { return $_ eq '0.000'; }, decimal => ',' );
"Values"
"-,456"
DATA

&test_csv_format_guessing
    (data => <<DATA, format => sub { return $_ eq '#,##0.00'; }, decimal => '.', thousand => ',' );
"Values"
"100,456.22"
DATA

&test_csv_format_guessing
    (data => <<DATA, format => sub { return $_ eq '#,##0.000'; }, decimal => '.', thousand => ',' );
"Values"
"100,456.222"
DATA

print STDERR "Pass\n";

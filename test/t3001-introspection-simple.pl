#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&setup_python_environment ();

my $python_script = $0;
$python_script =~ s/\.pl$/.py/;
my $ref = join("",<DATA>);
&test_command ($PYTHON . ' ' . &GnumericTest::quotearg ($python_script),
	       sub { $_ eq $ref });

__DATA__
Name: Sheet1
Number of columns: 256
Number of rows: 65536

As string:
10
101.25
111.25
01
zzz
abc
TRUE

As int:
10
101
111
1
0
0
1

Formatted value:
Value: 101.25  Format: 0.0000  Rendered: 101.2500

List of cells in sheet:
A1: 10 [bold]
C1: 10 
A2: 101.2500 [bold]
C2: 101.25 
A3: =A1+A2 
C3: =C1+C2 
A4: '01 
C4: '01 
A5: zzz 
C5: zzz 
A6: abc 
C6: abc 
A7: TRUE 
C7: TRUE 

#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $expected;
{ local $/; $expected = <DATA>; }

&message ("Check insert/delete col/row.");
&sstest ("test_insert_delete", $expected);

__DATA__
-----------------------------------------------------------------------------
Start: test_insert_delete
-----------------------------------------------------------------------------

# Init
B2: =D4+1
D2: =if(TRUE,B2,2)
# About to insert column before F
B2: =D4+1
D2: =if(TRUE,B2,2)
# About to insert column before E
B2: =D4+1
D2: =if(TRUE,B2,2)
# About to insert column before D
B2: =E4+1
E2: =if(TRUE,B2,2)
# About to insert column before C
B2: =F4+1
F2: =if(TRUE,B2,2)
# About to insert column before B
C2: =G4+1
G2: =if(TRUE,C2,2)
# About to insert column before A
D2: =H4+1
H2: =if(TRUE,D2,2)
# About to insert row before 6
D2: =H4+1
H2: =if(TRUE,D2,2)
# About to insert row before 5
D2: =H4+1
H2: =if(TRUE,D2,2)
# About to insert row before 4
D2: =H5+1
H2: =if(TRUE,D2,2)
# About to insert row before 3
D2: =H6+1
H2: =if(TRUE,D2,2)
# About to insert row before 2
D3: =H7+1
H3: =if(TRUE,D3,2)
# About to insert row before 1
D4: =H8+1
H4: =if(TRUE,D4,2)
# Undo the lot
B2: =D4+1
D2: =if(TRUE,B2,2)
# About to delete column F
B2: =D4+1
D2: =if(TRUE,B2,2)
# About to delete column E
B2: =D4+1
D2: =if(TRUE,B2,2)
# About to delete column D
B2: =#REF!+1
# About to delete column C
B2: =#REF!+1
# About to delete column B
# About to delete column A
# About to delete row 6
# About to delete row 5
# About to delete row 4
# About to delete row 3
# About to delete row 2
# About to delete row 1
# Undo the lot
B2: =D4+1
D2: =if(TRUE,B2,2)
End: test_insert_delete


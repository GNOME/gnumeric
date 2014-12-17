#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $expected;
{ local $/; $expected = <DATA>; }

&message ("Check insert/delete col/row's effect on names.");
&sstest ("test_insdel_rowcol_names", $expected);

__DATA__
-----------------------------------------------------------------------------
Start: test_insdel_rowcol_names
-----------------------------------------------------------------------------

Dumping names...
Scope=Sheet1 Name="NAMEA1" Expr=A1
Scope=Sheet1 Name="NAMEA1ABS" Expr=$A$1
Scope=Sheet1 Name="NAMEA2" Expr=A2
Scope=Sheet1 Name="NAMEA2ABS" Expr=$A$2
Scope=Sheet1 Name="Print_Area" Expr=Sheet1!$1:$65536
Scope=Sheet1 Name="Sheet_Title" Expr="Sheet1"
Scope=Sheet2 Name="Print_Area" Expr=Sheet2!$1:$65536
Scope=Sheet2 Name="Sheet_Title" Expr="Sheet2"
Scope=Global Name="NAMEG2" Expr=$A$14+Sheet1!$A$14+Sheet2!$A$14
Scope=Global Name="NAMEGA1" Expr=A1
Dumping names... Done
About to insert before column D on Sheet1
Dumping names...
Scope=Sheet1 Name="NAMEA1" Expr=A1
Scope=Sheet1 Name="NAMEA1ABS" Expr=$A$1
Scope=Sheet1 Name="NAMEA2" Expr=A2
Scope=Sheet1 Name="NAMEA2ABS" Expr=$A$2
Scope=Sheet1 Name="Print_Area" Expr=Sheet1!$1:$65536
Scope=Sheet1 Name="Sheet_Title" Expr="Sheet1"
Scope=Sheet2 Name="Print_Area" Expr=Sheet2!$1:$65536
Scope=Sheet2 Name="Sheet_Title" Expr="Sheet2"
Scope=Global Name="NAMEG2" Expr=$A$14+Sheet1!$A$14+Sheet2!$A$14
Scope=Global Name="NAMEGA1" Expr=A1
Dumping names... Done
Undoing.
Done.
About to insert before column C on Sheet1
Dumping names...
Scope=Sheet1 Name="NAMEA1" Expr=A1
Scope=Sheet1 Name="NAMEA1ABS" Expr=$A$1
Scope=Sheet1 Name="NAMEA2" Expr=A2
Scope=Sheet1 Name="NAMEA2ABS" Expr=$A$2
Scope=Sheet1 Name="Print_Area" Expr=Sheet1!$1:$65536
Scope=Sheet1 Name="Sheet_Title" Expr="Sheet1"
Scope=Sheet2 Name="Print_Area" Expr=Sheet2!$1:$65536
Scope=Sheet2 Name="Sheet_Title" Expr="Sheet2"
Scope=Global Name="NAMEG2" Expr=$A$14+Sheet1!$A$14+Sheet2!$A$14
Scope=Global Name="NAMEGA1" Expr=A1
Dumping names... Done
Undoing.
Done.
About to insert before column B on Sheet1
Dumping names...
Scope=Sheet1 Name="NAMEA1" Expr=A1
Scope=Sheet1 Name="NAMEA1ABS" Expr=$A$1
Scope=Sheet1 Name="NAMEA2" Expr=A2
Scope=Sheet1 Name="NAMEA2ABS" Expr=$A$2
Scope=Sheet1 Name="Print_Area" Expr=Sheet1!$1:$65536
Scope=Sheet1 Name="Sheet_Title" Expr="Sheet1"
Scope=Sheet2 Name="Print_Area" Expr=Sheet2!$1:$65536
Scope=Sheet2 Name="Sheet_Title" Expr="Sheet2"
Scope=Global Name="NAMEG2" Expr=$A$14+Sheet1!$A$14+Sheet2!$A$14
Scope=Global Name="NAMEGA1" Expr=A1
Dumping names... Done
Undoing.
Done.
About to insert before column A on Sheet1
Dumping names...
Scope=Sheet1 Name="NAMEA1" Expr=A1
Scope=Sheet1 Name="NAMEA1ABS" Expr=$M$1
Scope=Sheet1 Name="NAMEA2" Expr=A2
Scope=Sheet1 Name="NAMEA2ABS" Expr=$M$2
Scope=Sheet1 Name="Print_Area" Expr=Sheet1!$1:$65536
Scope=Sheet1 Name="Sheet_Title" Expr="Sheet1"
Scope=Sheet2 Name="Print_Area" Expr=Sheet2!$1:$65536
Scope=Sheet2 Name="Sheet_Title" Expr="Sheet2"
Scope=Global Name="NAMEG2" Expr=$A$14+Sheet1!$M$14+Sheet2!$A$14
Scope=Global Name="NAMEGA1" Expr=A1
Dumping names... Done
Undoing.
Done.
About to insert before column D on Sheet2
Dumping names...
Scope=Sheet1 Name="NAMEA1" Expr=A1
Scope=Sheet1 Name="NAMEA1ABS" Expr=$A$1
Scope=Sheet1 Name="NAMEA2" Expr=A2
Scope=Sheet1 Name="NAMEA2ABS" Expr=$A$2
Scope=Sheet1 Name="Print_Area" Expr=Sheet1!$1:$65536
Scope=Sheet1 Name="Sheet_Title" Expr="Sheet1"
Scope=Sheet2 Name="Print_Area" Expr=Sheet2!$1:$65536
Scope=Sheet2 Name="Sheet_Title" Expr="Sheet2"
Scope=Global Name="NAMEG2" Expr=$A$14+Sheet1!$A$14+Sheet2!$A$14
Scope=Global Name="NAMEGA1" Expr=A1
Dumping names... Done
Undoing.
Done.
About to insert before column C on Sheet2
Dumping names...
Scope=Sheet1 Name="NAMEA1" Expr=A1
Scope=Sheet1 Name="NAMEA1ABS" Expr=$A$1
Scope=Sheet1 Name="NAMEA2" Expr=A2
Scope=Sheet1 Name="NAMEA2ABS" Expr=$A$2
Scope=Sheet1 Name="Print_Area" Expr=Sheet1!$1:$65536
Scope=Sheet1 Name="Sheet_Title" Expr="Sheet1"
Scope=Sheet2 Name="Print_Area" Expr=Sheet2!$1:$65536
Scope=Sheet2 Name="Sheet_Title" Expr="Sheet2"
Scope=Global Name="NAMEG2" Expr=$A$14+Sheet1!$A$14+Sheet2!$A$14
Scope=Global Name="NAMEGA1" Expr=A1
Dumping names... Done
Undoing.
Done.
About to insert before column B on Sheet2
Dumping names...
Scope=Sheet1 Name="NAMEA1" Expr=A1
Scope=Sheet1 Name="NAMEA1ABS" Expr=$A$1
Scope=Sheet1 Name="NAMEA2" Expr=A2
Scope=Sheet1 Name="NAMEA2ABS" Expr=$A$2
Scope=Sheet1 Name="Print_Area" Expr=Sheet1!$1:$65536
Scope=Sheet1 Name="Sheet_Title" Expr="Sheet1"
Scope=Sheet2 Name="Print_Area" Expr=Sheet2!$1:$65536
Scope=Sheet2 Name="Sheet_Title" Expr="Sheet2"
Scope=Global Name="NAMEG2" Expr=$A$14+Sheet1!$A$14+Sheet2!$A$14
Scope=Global Name="NAMEGA1" Expr=A1
Dumping names... Done
Undoing.
Done.
About to insert before column A on Sheet2
Dumping names...
Scope=Sheet1 Name="NAMEA1" Expr=A1
Scope=Sheet1 Name="NAMEA1ABS" Expr=$A$1
Scope=Sheet1 Name="NAMEA2" Expr=A2
Scope=Sheet1 Name="NAMEA2ABS" Expr=$A$2
Scope=Sheet1 Name="Print_Area" Expr=Sheet1!$1:$65536
Scope=Sheet1 Name="Sheet_Title" Expr="Sheet1"
Scope=Sheet2 Name="Print_Area" Expr=Sheet2!$1:$65536
Scope=Sheet2 Name="Sheet_Title" Expr="Sheet2"
Scope=Global Name="NAMEG2" Expr=$A$14+Sheet1!$A$14+Sheet2!$M$14
Scope=Global Name="NAMEGA1" Expr=A1
Dumping names... Done
Undoing.
Done.
About to delete column D on Sheet1
Dumping names...
Scope=Sheet1 Name="NAMEA1" Expr=A1
Scope=Sheet1 Name="NAMEA1ABS" Expr=$A$1
Scope=Sheet1 Name="NAMEA2" Expr=A2
Scope=Sheet1 Name="NAMEA2ABS" Expr=$A$2
Scope=Sheet1 Name="Print_Area" Expr=Sheet1!$1:$65536
Scope=Sheet1 Name="Sheet_Title" Expr="Sheet1"
Scope=Sheet2 Name="Print_Area" Expr=Sheet2!$1:$65536
Scope=Sheet2 Name="Sheet_Title" Expr="Sheet2"
Scope=Global Name="NAMEG2" Expr=$A$14+Sheet1!$A$14+Sheet2!$A$14
Scope=Global Name="NAMEGA1" Expr=A1
Dumping names... Done
Undoing.
Done.
About to delete column C on Sheet1
Dumping names...
Scope=Sheet1 Name="NAMEA1" Expr=A1
Scope=Sheet1 Name="NAMEA1ABS" Expr=$A$1
Scope=Sheet1 Name="NAMEA2" Expr=A2
Scope=Sheet1 Name="NAMEA2ABS" Expr=$A$2
Scope=Sheet1 Name="Print_Area" Expr=Sheet1!$1:$65536
Scope=Sheet1 Name="Sheet_Title" Expr="Sheet1"
Scope=Sheet2 Name="Print_Area" Expr=Sheet2!$1:$65536
Scope=Sheet2 Name="Sheet_Title" Expr="Sheet2"
Scope=Global Name="NAMEG2" Expr=$A$14+Sheet1!$A$14+Sheet2!$A$14
Scope=Global Name="NAMEGA1" Expr=A1
Dumping names... Done
Undoing.
Done.
About to delete column B on Sheet1
Dumping names...
Scope=Sheet1 Name="NAMEA1" Expr=A1
Scope=Sheet1 Name="NAMEA1ABS" Expr=$A$1
Scope=Sheet1 Name="NAMEA2" Expr=A2
Scope=Sheet1 Name="NAMEA2ABS" Expr=$A$2
Scope=Sheet1 Name="Print_Area" Expr=Sheet1!$1:$65536
Scope=Sheet1 Name="Sheet_Title" Expr="Sheet1"
Scope=Sheet2 Name="Print_Area" Expr=Sheet2!$1:$65536
Scope=Sheet2 Name="Sheet_Title" Expr="Sheet2"
Scope=Global Name="NAMEG2" Expr=$A$14+Sheet1!$A$14+Sheet2!$A$14
Scope=Global Name="NAMEGA1" Expr=A1
Dumping names... Done
Undoing.
Done.
About to delete column A on Sheet1
Dumping names...
Scope=Sheet1 Name="NAMEA1" Expr=A1
Scope=Sheet1 Name="NAMEA1ABS" Expr=#REF!
Scope=Sheet1 Name="NAMEA2" Expr=A2
Scope=Sheet1 Name="NAMEA2ABS" Expr=#REF!
Scope=Sheet1 Name="Print_Area" Expr=Sheet1!$1:$65536
Scope=Sheet1 Name="Sheet_Title" Expr="Sheet1"
Scope=Sheet2 Name="Print_Area" Expr=Sheet2!$1:$65536
Scope=Sheet2 Name="Sheet_Title" Expr="Sheet2"
Scope=Global Name="NAMEG2" Expr=$A$14+#REF!+Sheet2!$A$14
Scope=Global Name="NAMEGA1" Expr=A1
Dumping names... Done
Undoing.
Done.
End: test_insdel_rowcol_names

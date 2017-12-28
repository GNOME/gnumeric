#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check ssconvert resize");

my $src = "$samples/regress.gnumeric";
my $tmp = "regress-resize.gnumeric";
&GnumericTest::junkfile ($tmp);

# Shrink rows
&test_command ("$ssconvert --resize=8192x256 $src $tmp", sub { 1 } );
&test_command ("($ssdiff --xml $src $tmp ; true)",
	       sub {
		   $_ eq <<DIFF
<?xml version="1.0" encoding="UTF-8"?>
<s:Diff xmlns:s="http://www.gnumeric.org/ssdiff.dtd">
  <s:Sheet Name="Sheet1" Old="0" New="0">
    <s:Rows Old="65536" New="8192"/>
  </s:Sheet>
</s:Diff>
DIFF
	       });

# Shrink cols
&test_command ("$ssconvert --resize=65536x128 $src $tmp", sub { 1 } );
&test_command ("($ssdiff --xml $src $tmp ; true)",
	       sub {
		   $_ eq <<DIFF
<?xml version="1.0" encoding="UTF-8"?>
<s:Diff xmlns:s="http://www.gnumeric.org/ssdiff.dtd">
  <s:Sheet Name="Sheet1" Old="0" New="0">
    <s:Cols Old="256" New="128"/>
  </s:Sheet>
</s:Diff>
DIFF
	       });

# Shrink both
&test_command ("$ssconvert --resize=8192x128 $src $tmp", sub { 1 } );
&test_command ("($ssdiff --xml $src $tmp ; true)",
	       sub {
		   $_ eq <<DIFF
<?xml version="1.0" encoding="UTF-8"?>
<s:Diff xmlns:s="http://www.gnumeric.org/ssdiff.dtd">
  <s:Sheet Name="Sheet1" Old="0" New="0">
    <s:Cols Old="256" New="128"/>
    <s:Rows Old="65536" New="8192"/>
  </s:Sheet>
</s:Diff>
DIFF
	       });

# Expand rows
&test_command ("$ssconvert --resize=1048576x256 $src $tmp", sub { 1 } );
&test_command ("($ssdiff --xml $src $tmp ; true)",
	       sub {
		   $_ eq <<DIFF
<?xml version="1.0" encoding="UTF-8"?>
<s:Diff xmlns:s="http://www.gnumeric.org/ssdiff.dtd">
  <s:Sheet Name="Sheet1" Old="0" New="0">
    <s:Rows Old="65536" New="1048576"/>
  </s:Sheet>
</s:Diff>
DIFF
	       });

# Expand cols
&test_command ("$ssconvert --resize=65536x512 $src $tmp", sub { 1 } );
&test_command ("($ssdiff --xml $src $tmp ; true)",
	       sub {
		   $_ eq <<DIFF
<?xml version="1.0" encoding="UTF-8"?>
<s:Diff xmlns:s="http://www.gnumeric.org/ssdiff.dtd">
  <s:Sheet Name="Sheet1" Old="0" New="0">
    <s:Cols Old="256" New="512"/>
  </s:Sheet>
</s:Diff>
DIFF
	       });

# Expand both
&test_command ("$ssconvert --resize=1048576x16384 $src $tmp", sub { 1 } );
&test_command ("($ssdiff --xml $src $tmp ; true)",
	       sub {
		   $_ eq <<DIFF
<?xml version="1.0" encoding="UTF-8"?>
<s:Diff xmlns:s="http://www.gnumeric.org/ssdiff.dtd">
  <s:Sheet Name="Sheet1" Old="0" New="0">
    <s:Cols Old="256" New="16384"/>
    <s:Rows Old="65536" New="1048576"/>
  </s:Sheet>
</s:Diff>
DIFF
	       });

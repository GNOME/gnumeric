#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $file = "$samples/graph-tests.gnumeric";

&message ("Check graph gnumeric roundtrip.");
&test_roundtrip ($file,
		 'format' => 'Gnumeric_XmlIO:sax',
		 'ext' => "gnm",
		 'ignore_failure' => 1);

# my $ods_auto_filter = "$PERL -p -e 's{auto-dash=\"1\"}{auto-dash=\"0\" dash=\"solid\"}'";

&message ("Check graph ods roundtrip.");
&test_roundtrip ($file,
		 'format' => 'Gnumeric_OpenCalc:odf',
		 'ext' => "ods",
#		 'filter1' => $ods_auto_filter,
#		 'filter2' => "$ods_auto_filter | $PERL -p -e '\$_ = \"\" if m{<meta:generator>}'",
		 'filter2' => "$PERL -p -e '\$_ = \"\" if m{<meta:generator>}'",
		 'ignore_failure' => 1);

my $xls_codepage_filter = "$PERL -p -e '\$_ = \"\" if m{<meta:user-defined meta:name=.msole:codepage.}'";

if (0) {
    # We don't save graphs, so don't test.
    &message ("Check graph xls/BIFF7 roundtrip.");
    &test_roundtrip ($file,
		     'format' => 'Gnumeric_Excel:excel_biff7',
		     'ext' => "xls",
		     'resize' => '16384x256',
		     'filter2' => $xls_codepage_filter);
}

&message ("Check graph xls/BIFF8 roundtrip.");
&test_roundtrip ($file,
		 'format' => 'Gnumeric_Excel:excel_biff8',
		 'ext' => "xls",
		 'filter2' => $xls_codepage_filter,
		 'ignore_failure' => 1);

&message ("Check graph xlsx roundtrip.");
&test_roundtrip ($file,
		 'format' => 'Gnumeric_Excel:xlsx',
		 'ext' => "xlsx",
		 'resize' => '1048576x16384',
		 'ignore_failure' => 1);

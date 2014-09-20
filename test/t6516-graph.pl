#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $file = "$samples/graph-tests.gnumeric";

&message ("Check graph gnumeric roundtrip.");
&test_roundtrip ($file,
		 'format' => 'Gnumeric_XmlIO:sax',
		 'ext' => "gnm");

# ods doesn't have outline colour, so copy the fill colour.
my $ods_outline_filter = "$PERL -p -e 'if (/\\bmarker\\b.*fill-color=\"([A-Z0-9:]+)\"/) { my \$col = \$1; s{\\b(outline-color)=\"[A-Z0-9:]+\"}{\$1=\"\$col\"}; }'";

&message ("Check graph ods roundtrip.");
&test_roundtrip ($file,
		 'format' => 'Gnumeric_OpenCalc:odf',
		 'ext' => "ods",
		 'filter1' => $ods_outline_filter,
		 'filter2' => 'std:drop_generator',
		 'ignore_failure' => 1);

if (0) {
    # We don't save graphs, so don't test.
    &message ("Check graph xls/BIFF7 roundtrip.");
    &test_roundtrip ($file,
		     'format' => 'Gnumeric_Excel:excel_biff7',
		     'ext' => "xls",
		     'resize' => '16384x256',
		     'filter2' => 'std:drop_codepage');
}

&message ("Check graph xls/BIFF8 roundtrip.");
&test_roundtrip ($file,
		 'format' => 'Gnumeric_Excel:excel_biff8',
		 'ext' => "xls",
		 'filter2' => 'std:drop_codepage',
		 'ignore_failure' => 1);

&message ("Check graph xlsx roundtrip.");
&test_roundtrip ($file,
		 'format' => 'Gnumeric_Excel:xlsx',
		 'ext' => "xlsx",
		 'resize' => '1048576x16384',
		 'ignore_failure' => 1);

#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check ssindex.");

sub uniq { my %h; map { $h{$_} = 1} @_; return keys %h; }

sub compare_items {
    my ($p1,$p2) = @_;
    $p1 = [sort (&uniq (@$p1))];
    $p2 = [sort (&uniq (@$p2))];
    return 0 unless @$p1 eq @$p2;
    while (@$p1) {
	return 0 unless shift (@$p1) eq shift (@$p2);
    }
    return 1;
}

# -----------------------------------------------------------------------------

my @expected_statfuns =
    ('#Succeded', '#Total', '1st test', '2nd test', '3rd test', 'AVEDEV',
     'AVERAGE', 'AVERAGEA', 'Accuracy Limit', 'All ok.', 'BETADIST',
     'BETAINV', 'BINOMDIST', 'CHIDIST', 'CHIINV', 'CHITEST', 'CONFIDENCE',
     'CORREL', 'COUNT', 'COUNTA', 'COVAR', 'CRITBINOM', 'Correct', 'DEVSQ',
     'EXPONDIST', 'FDIST', 'FINV', 'FISHER', 'FISHERINV', 'FORECAST',
     'FREQUENCY', 'FTEST', 'Function', 'GAMMADIST', 'GAMMAINV', 'GAMMALN',
     'GEOMEAN', 'GROWTH', 'HARMEAN', 'HYPGEOMDIST', 'INTERCEPT', 'KURT',
     'LARGE', 'LINEST', 'LOGEST', 'LOGINV', 'LOGNORMDIST', 'MAX', 'MAXA',
     'MEDIAN', 'MIN', 'MINA', 'MODE', 'NEGBINOMDIST', 'NORMDIST',
     'NORMINV', 'NORMSDIST', 'NORMSINV', 'Ok.', 'PEARSON', 'PERCENTILE',
     'PERCENTRANK', 'PERMUT', 'POISSON', 'PROB', 'QUARTILE', 'RANK', 'RSQ',
     'SKEW', 'SLOPE', 'SMALL', 'STANDARDIZE', 'STATISTICAL FUNCTIONS',
     'STDEV', 'STDEVA', 'STDEVP', 'STDEVPA', 'STEYX', 'Sheet1', 'Sheet10',
     'Sheet11', 'Sheet12', 'Sheet13', 'Sheet14', 'Sheet15', 'Sheet16',
     'Sheet2', 'Sheet3', 'Sheet4', 'Sheet5', 'Sheet6', 'Sheet7', 'Sheet8',
     'Sheet9', 'Status', 'Status message', 'TDIST', 'TINV', 'TREND',
     'TRIMMEAN', 'TTEST', 'Test Data:', 'Test Status', 'Total', 'VAR',
     'VARA', 'VARP', 'VARPA', 'WEIBULL', 'ZTEST', '[0..1]', 'manytypes',
     'mode', 'neg', 'pos&neg', 'same', 'text', 'x', 'y', 'z', 'Print_Area',
     'Sheet10', 'Sheet11', 'Sheet12', 'Sheet13', 'Sheet14', 'Sheet15',
     'Sheet16', 'Sheet2', 'Sheet3', 'Sheet4', 'Sheet5', 'Sheet6', 'Sheet7',
     'Sheet8', 'Sheet9', 'Sheet_Title',
     );

&test_ssindex ("$samples/excel/statfuns.xls",
	       (sub { &compare_items ($_, \@expected_statfuns); }));

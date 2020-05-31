#!/usr/bin/perl -w

# This script processes the test cases from amath, see
# http://www.wolfgang-ehrhardt.de/amath_functions.html

use strict;

my $debug_underflow = 0;
my $debug_overflow = 0;
my $debug_arguments = 1;
my $dir = $ARGV[0];
die "$0: missing amath directory\n" unless (defined $dir) && -d $dir;

my @test_files =
    ('t_sfd1a.pas',
     't_sfd1.pas',
     't_sfd3a.pas',
     't_sfd3b.pas',
     't_sfd3c.pas',
     't_sfd3.pas',
     't_sfd4.pas',
     't_sfd6.pas',
     't_amath1.pas',
     't_amathm.pas',
    );

my %name_map =
    ('lnbeta' => 'betaln',
     'beta' => 'beta',
     'lngamma' => 'gammaln',
     'gamma' => 'gamma',
     'fac' => 'fact',		# no actual tests
     'dfac' => 'factdouble',
     'pochhammer' => 'pochhammer',
     'binomial' => 'combin',
     'cauchy_cdf' => 'r.pcauchy',
     'cauchy_inv' => 'r.qcauchy',
     'cauchy_pdf' => 'r.dcauchy',
     'chi2_cdf' => 'r.pchisq',
     'chi2_inv' => 'r.qchisq',
     'chi2_pdf' => 'r.dchisq',
     'exp_cdf' => 'r.pexp',
     'exp_inv' => 'r.qexp',
     'exp_pdf' => 'r.dexp',
     'gamma_cdf' => 'r.pgamma',
     'gamma_inv' => 'r.qgamma',
     'gamma_pdf' => 'r.dgamma',
     'laplace_pdf' => 'laplace',
     'logistic_pdf' => 'logistic',
     'lognormal_cdf' => 'r.plnorm',
     'lognormal_inv' => 'r.qlnorm',
     'lognormal_pdf' => 'r.dlnorm',
     'pareto_pdf' => 'pareto',
     'weibull_cdf' => 'r.pweibull',
     'weibull_inv' => 'r.qweibull',
     'weibull_pdf' => 'r.dweibull',
     'binomial_pmf' => 'r.dbinom',
     'binomial_cdf' => 'r.pbinom',
     'poisson_pmf' => 'r.dpois',
     'poisson_cdf' => 'r.ppois',
     'negbinom_pmf' => 'r.dnbinom',
     'negbinom_cdf' => 'r.pnbinom',
     'hypergeo_pmf' => 'r.dhyper',
     'hypergeo_cdf' => 'r.phyper',
     'rayleigh_pdf' => 'rayleigh',
     'normal_cdf' => 'r.pnorm',
     'normal_inv' => 'r.qnorm',
     'normal_pdf' => 'r.dnorm',
     'beta_cdf' => 'r.pbeta',
     'beta_inv' => 'r.qbeta',
     'beta_pdf' => 'r.dbeta',
     't_cdf' => 'r.pt',
     't_inv' => 'r.qt',
     't_pdf' => 'r.dt',
     'f_cdf' => 'r.pf',
     'f_inv' => 'r.qf',
     'f_pdf' => 'r.df',
     'erf' => 'erf',
     'erfc' => 'erfc',
     'bessel_j0' => 'besselj0', # Really named besselj
     'bessel_j1' => 'besselj1', # Really named besselj
     'bessel_jv' => 'besselj',
     'bessel_y0' => 'bessely0', # Really named bessely
     'bessel_y1' => 'bessely1', # Really named bessely
     'bessel_yv' => 'bessely',
     'bessel_i0' => 'besseli0', # Really named besseli
     'bessel_i1' => 'besseli1', # Really named besseli
     'bessel_iv' => 'besseli',
     'bessel_k0' => 'besselk0', # Really named besselk
     'bessel_k1' => 'besselk1', # Really named besselk
     'bessel_kv' => 'besselk',
     'exp' => 'exp',
     'exp2' => 'exp2',
     'exp10' => 'exp10',
     'expm1' => 'expm1',
     'ln' => 'ln',
     'ln1p' => 'ln1p',
     'log10' => 'log10',
     'log2' => 'log2',
     'arccos' => 'acos',
     'arccosh' => 'acosh',
     'arcsin' => 'asin',
     'arcsinh' => 'asinh',
     'arccot' => 'acot',
     'arccoth' => 'acoth',
     'arctan' => 'atan',
     'arctanh' => 'atanh',
     'cos' => 'cos',
     'cosh' => 'cosh',
     'cot' => 'cot',
     'coth' => 'coth',
     'csc' => 'csc',
     'csch' => 'csch',
     'sec' => 'sec',
     'sech' => 'sech',
     'sin' => 'sin',
     'sinh' => 'sinh',
     'tan' => 'tan',
     'tanh' => 'tanh',
     'gd' => 'gd',
    );

my %invalid_tests =
    (# Magically changed to something else
     'cos(1.0)' => 1,
     'cos(0.0)' => 1,
     'cos(1e26)' => 1, # 1e26 not representable
     'cot(1e26)' => 1, # 1e26 not representable
     'sin(1e26)' => 1, # 1e26 not representable
     'tan(1e26)' => 1, # 1e26 not representable

     # Just plain wrong (and would depend on representation anyway)
     'besselj(11.791534439014281614,0)' => 1,
     'besselj(13.323691936314223032,1)' => 1,
     'bessely(13.36109747387276348,0)' => 1,
     'bessely(14.89744212833672538,1)' => 1,

     # Overflow, not zero
     'bessely(1.5,-1700.5)' => 1,
    );

sub def_expr_handler {
    my ($f,$pa) = @_;
    my $expr = "$f(" . join (",", @$pa) . ")";
    return undef if exists $invalid_tests{$expr};
    return $expr;
}

my %expr_handlers =
    ('beta' => \&non_negative_handler,
     'gammaln' => \&non_negative_handler,
     'factdouble' => \&non_negative_handler,
     'combin' => \&non_negative_handler,
     'r.dcauchy' => sub { &reorder_handler ("3,1,2", @_); },
     'r.pcauchy' => sub { &reorder_handler ("3,1,2", @_); },
     'r.qcauchy' => sub { &reorder_handler ("3,1,2", @_); },
     'r.dchisq' => sub { &reorder_handler ("2,1", @_); },
     'r.pchisq' => sub { &reorder_handler ("2,1", @_); },
     'r.qchisq' => sub { &reorder_handler ("2,1", @_); },
     'r.dexp' => sub { my ($f,$pa) = @_; &def_expr_handler ($f,["$pa->[2]-$pa->[0]","1/$pa->[1]"]); },
     'r.pexp' => sub { my ($f,$pa) = @_; &def_expr_handler ($f,["$pa->[2]-$pa->[0]","1/$pa->[1]"]); },
     'r.qexp' => sub { my ($f,$pa) = @_; &def_expr_handler ($f,[$pa->[2],"1/$pa->[1]"]) . "+$pa->[0]"; },
     'r.dgamma' => sub { &reorder_handler ("3,1,2", @_); },
     'r.pgamma' => sub { &reorder_handler ("3,1,2", @_); },
     'r.qgamma' => sub { &reorder_handler ("3,1,2", @_); },
     'laplace' => sub { my ($f,$pa) = @_; &def_expr_handler ($f,["$pa->[2]-$pa->[0]",$pa->[1]]); },
     'logistic' => sub { my ($f,$pa) = @_; &def_expr_handler ($f,["$pa->[2]-$pa->[0]",$pa->[1]]); },
     'r.dlnorm' => sub { &reorder_handler ("3,1,2", @_); },
     'r.plnorm' => sub { &reorder_handler ("3,1,2", @_); },
     'r.qlnorm' => sub { &reorder_handler ("3,1,2", @_); },
     'pareto' => sub { &reorder_handler ("3,2,1", @_); },
     'r.dweibull' => sub { &reorder_handler ("3,1,2", @_); },
     'r.pweibull' => sub { &reorder_handler ("3,1,2", @_); },
     'r.qweibull' => sub { &reorder_handler ("3,1,2", @_); },
     'r.dbinom' => sub { &reorder_handler ("3,2,1", @_); },
     'r.pbinom' => sub { &reorder_handler ("3,2,1", @_); },
     'r.dpois' => sub { &reorder_handler ("2,1", @_); },
     'r.ppois' => sub { &reorder_handler ("2,1", @_); },
     'r.dnbinom' => sub { &reorder_handler ("3,2,1", @_); },
     'r.pnbinom' => sub { &reorder_handler ("3,2,1", @_); },
     'r.dhyper' => sub { &reorder_handler ("4,1,2,3", @_); },
     'r.phyper' => sub { &reorder_handler ("4,1,2,3", @_); },
     'rayleigh' => sub { &reorder_handler ("2,1", @_); },
     'r.dnorm' => sub { &reorder_handler ("3,1,2", @_); },
     'r.pnorm' => sub { &reorder_handler ("3,1,2", @_); },
     'r.qnorm' => sub { &reorder_handler ("3,1,2", @_); },
     'r.dbeta' => sub { &reorder_handler ("3,1,2", @_); },
     'r.pbeta' => sub { &reorder_handler ("3,1,2", @_); },
     'r.qbeta' => sub { &reorder_handler ("3,1,2", @_); },
     'r.dt' => sub { &reorder_handler ("2,1", @_); },
     'r.pt' => sub { &reorder_handler ("2,1", @_); },
     'r.qt' => sub { &reorder_handler ("2,1", @_); },
     'r.df' => sub { &reorder_handler ("3,1,2", @_); },
     'r.pf' => sub { &reorder_handler ("3,1,2", @_); },
     'r.qf' => sub { &reorder_handler ("3,1,2", @_); },
     'besselj0' => sub { my ($f,$pa) = @_; &def_expr_handler ('besselj',[@$pa,0]); },
     'besselj1' => sub { my ($f,$pa) = @_; &def_expr_handler ('besselj',[@$pa,1]); },
     'besselj' => sub { &reorder_handler ("2,1", @_); },
     'bessely0' => sub { my ($f,$pa) = @_; &def_expr_handler ('bessely',[@$pa,0]); },
     'bessely1' => sub { my ($f,$pa) = @_; &def_expr_handler ('bessely',[@$pa,1]); },
     'bessely' => sub { &reorder_handler ("2,1", @_); },
     'besseli0' => sub { my ($f,$pa) = @_; &def_expr_handler ('besseli',[@$pa,0]); },
     'besseli1' => sub { my ($f,$pa) = @_; &def_expr_handler ('besseli',[@$pa,1]); },
     'besseli' => sub { &reorder_handler ("2,1", @_); },
     'besselk0' => sub { my ($f,$pa) = @_; &def_expr_handler ('besselk',[@$pa,0]); },
     'besselk1' => sub { my ($f,$pa) = @_; &def_expr_handler ('besselk',[@$pa,1]); },
     'besselk' => sub { &reorder_handler ("2,1", @_); },
     'exp2' => sub { my ($f,$pa) = @_; &def_expr_handler ('power',[2,@$pa]); },
     'exp10' => sub { my ($f,$pa) = @_; &def_expr_handler ('power',[10,@$pa]); },
     'ln' => \&positive_handler,
     'log10' => \&positive_handler,
     'log2' => \&positive_handler,
    );

my %constants =
    # Use lower case.
    ('pi_1' => 3.1415926535897932385,
     'pi_2' => 1.5707963267948966192,
     'pi_3' => 1.0471975511965977462,
     'pi_4' => 0.78539816339744830962,
     'pi_6' => 0.52359877559829887308,
     'sqrt2' => 1.4142135623730950488,
     '-sqrt2' => -1.4142135623730950488,
     'sqrt3' => 1.7320508075688772935,
     '-sqrt3' => 1.7320508075688772935,
     'sqrt_5' => 0.7071067811865475244,
    );

# -----------------------------------------------------------------------------

my $last_func = '';
my @test_lines = ();

sub output_test {
    my ($gfunc,$expr,$res) = @_;

    my $gfunc0 = ($gfunc eq $last_func) ? '' : $gfunc;
    $res = "=$res" if $res =~ m{[*/]};

    my $N = 1 + @test_lines;
    push @test_lines, "\"$gfunc0\",\"=$expr\",\"$res\",\"=IF(B$N=C$N,\"\"\"\",IF(C$N=0,-LOG10(ABS(B$N)),-LOG10(ABS((B$N-C$N)/C$N))))\"";

    $last_func = $gfunc;
}

# -----------------------------------------------------------------------------

sub interpret_number {
    my ($s) = @_;

    if ($s =~ /^[-+]?(\d+\.?|\d*\.\d+)([eE][-+]?\d+)?$/) {
	return $s;
    } else {
	return undef;
    }
}

# -----------------------------------------------------------------------------

sub reorder_handler {
    my ($order,$f,$pargs) = @_;

    my @res;
    foreach (split (',',$order)) {
	push @res, $pargs->[$_ - 1];
    }

    return &def_expr_handler ($f,\@res);
}

sub non_negative_handler {
    my ($f,$pargs) = @_;

    foreach (@$pargs) {
	my $x = &interpret_number ($_);
	return undef unless defined ($x) && $x >= 0;
    }

    return &def_expr_handler ($f,$pargs);
}

sub positive_handler {
    my ($f,$pargs) = @_;

    foreach (@$pargs) {
	my $x = &interpret_number ($_);
	return undef unless defined ($x) && $x > 0;
    }

    return &def_expr_handler ($f,$pargs);
}

# -----------------------------------------------------------------------------

sub simplify_val {
    my ($val,$pvars) = @_;

    $val =~ s/^\s+//;
    $val =~ s/\s+$//;

    # Avoid a perl bug that underflows 0.153e-305
    while ($val =~ /^(.*)\b0\.(\d)(\d*)[eE]-(\d+)\b(.*)$/) {
	$val = "$1$2.$3e-" . ($4 + 1) . $5;
    }

    $val =~ s/\bldexp\s*\(\s*([-+.eE0-9_]+)\s*[,;]\s*([-+]?\d+)\s*\)/($1*2^$2)/g;

    if ($val =~ m{^[-+*/^() .eE0-9]+$}) {
	if ($val =~ /^[-+]?[0-9.]+[eE][-+]?\d+$/) {
	    if ($val == 0) {
		print STDERR "DEBUG: $val --> 0\n" if $debug_underflow;
		return 0;
	    }
	    if (($val + 0) =~ /inf/ ) {
		print STDERR "DEBUG: $val --> inf\n" if $debug_overflow;
		return undef;
	    }
	}

	return $val;
    } elsif (exists $pvars->{lc $val}) {
	return $pvars->{lc $val};
    } else {
	print STDERR "DEBUG: Argument $val unresolved.\n" if $debug_arguments;
	return undef;
    }
}

# -----------------------------------------------------------------------------

push @test_lines, ("") x 100;

my $func_no = 0;
foreach my $f (@test_files) {
    my $fn = "$dir/tests/$f";

    my ($afunc,$gfunc);

    my %vars;
    my $expr;

    my $first_row = 1 + @test_lines;

    open (my $src, "<", $fn) or die "$0: Cannot read $fn: $!\n";
    while (<$src>) {
	last if /^implementation\b/;
    }


    while (<$src>) {
	if (/^procedure\s+test_([a-zA-Z0-9_]+)\s*;/) {
	    $afunc = $1;
	    $gfunc = $name_map{$afunc};
	    printf STDERR "Reading tests for $gfunc\n" if $gfunc;
	    %vars = %constants;
	    next;
	}

	next unless defined $gfunc;

	if (/^end;/i) {
	    my $last_row = @test_lines;
	    if ($last_row >= $first_row) {
		my $count = $last_row - $first_row + 1;
		$test_lines[$func_no + 2] =
		    "$gfunc,$count,\"=min(D${first_row}:D${last_row},99)\"";
		$func_no++;
		$first_row = $last_row + 1;
	    }
	}

	if (s/^\s*y\s*:=\s*([a-zA-Z0-9_]+)\s*\(([^;{}]+)\)\s*;// &&
	    $1 eq $afunc) {
	    my $argtxt = $2;

	    $argtxt =~ s/\bldexp\s*\(\s*([-+.eE0-9_]+)\s*,\s*([-+]?\d+)\s*\)/ldexp($1;$2)/;
	    my @args = split (',',$argtxt);
	    my $ok = 1;

	    foreach (@args) {
		$_ = &simplify_val ($_, \%vars);
		if (!defined $_) {
		    $ok = 0;
		    last;
		}
	    }
	    next unless $ok;

	    my $h = $expr_handlers{$gfunc} || \&def_expr_handler;
	    $expr = &$h ($gfunc,\@args);
	}

	while (s/^\s*([a-zA-Z0-9]+)\s*:=\s*([^;]+)\s*;//) {
	    my $var = lc $1;
	    my $val = $2;
	    $val = &simplify_val ($val, \%vars);
	    if (defined $val) {
		$vars{$var} = $val;
	    } else {
		delete $vars{$var};
	    }
	}

	if (/^\s*test(rel|rele|abs)\s*/ && exists $vars{'f'} && defined ($expr)) {
	    &output_test ($gfunc, $expr, $vars{'f'});
	    $expr = undef;
	}

	if (/^\s*TData:\s*array/ ... /;\s*(\{.*\}\s*)*$/) {
	    if (/^\s*\(\s*tx\s*:([^;]+);\s*ty\s*:([^\)]+)\)\s*,?\s*$/) {
		my $tx = $1;
		my $ty = $2;
		my $x = &simplify_val ($tx, \%constants);
		my $y = &simplify_val ($ty, \%constants);
		my $h = $expr_handlers{$gfunc} || \&def_expr_handler;
		my $expr = (defined $x) ? &$h ($gfunc,[$x]) : undef;
		if (defined ($expr) && defined ($y)) {
		    &output_test ($gfunc, $expr, $y);
		}
	    }
	}
    }
}
{
    my $r0 = 3;
    my $r1 = $func_no + 2;
    $test_lines[0] = "\"Function\",\"Number of Tests\",\"Accuracy\",\"=min(C${r0}:C${r1})\"";
}

foreach (@test_lines) {
    print "$_\n";
}

# -----------------------------------------------------------------------------

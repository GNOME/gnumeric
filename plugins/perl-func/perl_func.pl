#!/usr/bin/perl

sub func_perl_adder {
  my($a,$b) = @_;
  return $a + $b;
}

sub help_perl_adder {
  return<<'EOS';
@FUNCTION=PERL_ADDER
@SYNTAX=PERL_ADDER(a,b)
@DESCRIPTION=
Adds two numbers. It is just an example function.
EOS
}

sub desc_perl_adder {
  return ("ff", "a,b,c");
}

sub func_perl_date {
  my ($sec,$min,$hour,$mday,$mon,$year,$wday,$yday,$isdst) = localtime;
  return sprintf("%04d%02d%02d", $year + 1900, ++$mon, $mday);
}

sub help_perl_date {
  return<<'EOS';
@FUNCTION=PERL_DATE
@SYNTAX=PERL_DATE()
@DESCRIPTION=
Return today's date as string.
EOS
}

sub desc_perl_date {
  return ("", "");
}

sub func_perl_sed {
  my $arg = shift;
  my $match = shift;
  my $newch = shift;

  $arg =~ s/$match/$newch/g;

  return $arg;
}

sub help_perl_sed {
  return<<'EOS';
@FUNCTION=PERL_SED
@SYNTAX=PERL_SED(a,b,c)
@DESCRIPTION=
Substite string with matching pattern. Same as $a =~ s/$b/$c/g .
EOS
}

sub desc_perl_sed {
  return ("sss", "a,b,c");
}

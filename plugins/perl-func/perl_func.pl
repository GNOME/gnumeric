#!/usr/bin/perl

sub func_perl_adder {
  my($a,$b) = @_;
  return $a + $b;
}

sub help_perl_adder {
  return ($GNM_FUNC_HELP_NAME, "PERL_ADDER:adds two numbers",
	  $GNM_FUNC_HELP_ARG, "a:number",
	  $GNM_FUNC_HELP_ARG, "b:number",
	  $GNM_FUNC_HELP_DESCRIPTION, "Adds two numbers. It is just an example function.",
	  $GNM_FUNC_HELP_EXAMPLES, "=PERL_ADDER(17,22)");
}

sub desc_perl_adder {
  return "ff";
}

sub func_perl_date {
  my ($sec,$min,$hour,$mday,$mon,$year,$wday,$yday,$isdst) = localtime;
  return sprintf("%04d%02d%02d", $year + 1900, ++$mon, $mday);
}

sub help_perl_date {
  return ($GNM_FUNC_HELP_NAME, "PERL_DATE:today's date",
	  $GNM_FUNC_HELP_DESCRIPTION, "Return today's date as string.",
	  $GNM_FUNC_HELP_EXAMPLES, "=PERL_DATE()");
}

sub desc_perl_date {
  return "";
}

sub func_perl_sed {
  my $arg = shift;
  my $match = shift;
  my $newch = shift;

  $arg =~ s/$match/$newch/g;

  return $arg;
}

sub help_perl_sed {
  return ($GNM_FUNC_HELP_NAME, "PERL_SED:string substitution",
	  $GNM_FUNC_HELP_ARG, "a:string",
	  $GNM_FUNC_HELP_ARG, "b:string",
	  $GNM_FUNC_HELP_ARG, "c:string",
	  $GNM_FUNC_HELP_DESCRIPTION, <<'EOS',
Substitute string with matching pattern. Same as $@{a} =~ s/$@{b}/$@{c}/g.
EOS
	  $GNM_FUNC_HELP_EXAMPLES, "=PERL_SED(\"abc\",\"b\",\"d\")");
}

sub desc_perl_sed {
  return "sss";
}

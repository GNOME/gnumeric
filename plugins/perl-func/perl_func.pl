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

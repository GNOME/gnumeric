# TODO :
# Change this file to read in other Perl files and not register the
# example function!!!
#

use Gnumeric;

sub foobar
{
  my($a,$b) = @_;
  print "Adding $a and $b.\n";
  return $a + $b;
}

$help_foobar = <<'EOS';
@FUNCTION=PERL_ADDER
@SYNTAX=PERL_ADDER(a,b)
@DESCRIPTION=
Adds two numbers. It is just an example function.
EOS

Gnumeric::register_function("perl_adder", "ff", "a,b", $help_foobar, \&foobar);

print "Hello World.\n";


#
# Perl interface to Gnumeric functions.
#

package Gnumeric;

use strict;
use vars qw($VERSION @ISA @EXPORT @EXPORT_OK);

$VERSION = "0.10";

require Exporter;
require DynaLoader;

@ISA = qw(Exporter DynaLoader);
@EXPORT = qw();
@EXPORT_OK = qw();
bootstrap Gnumeric $VERSION;

1;


#!/usr/bin/perl
# Task: convert gnumeric's function documentation into a valid DocBook XML
# fragment.

# Input format: as produced by "sstest --dump-func-defs=file", i.e.
# a series of chunks documenting functions. The chunks consist of a number
# of lines. Lines are either
# - @KEYWORD=value
# - @{parameter name}: text
# - a continuation of the previous line
# Chunks are separated by empty lines, but note that lines in a chunk may
# be empty, so empty lines are not always chunk separators.
# Chunks may contain multiple @KEYWORD=value lines for the same keyword.
# The various keywords have their own enum value in GnmFuncHelpType in
# src/func.h .

use strict;
use warnings;
use diagnostics;
use open IO => ':utf8';
binmode STDOUT, ':utf8';

#
# Global state that we need to track
#

# On the input side (parser state):
my $curcat = undef;	# Current category
my $curfunc = undef;	# Current function name
my $curkeyword = undef;	# Current input marker (keyword)

# On the output side:
my @tagstack = ();	# Closing tags that still need to be output at some
			# future point for proper balance.

#
# Helper functions
#

sub quote_stuff($) {
	# Escape/quote the characters which are special in XML: ampersand,
	# less than sign and greater than sign.
	my ($str) = @_;

	# Let's do this one first...
	$str =~ s/\&/\&amp;/gu;

	$str =~ s/</\&lt;/gu;
	$str =~ s/>/\&gt;/gu;
	return $str;
}

sub markup_stuff($) {
	my ($str) = @_;

	$str = &quote_stuff ($str);
	$str =~ s/\b$curfunc\b/<function>$curfunc<\/function>/gu;
	$str =~ s/\@\{([^}]*)\}/<parameter>$1<\/parameter>/gu;
	$str =~ s/\@(\w*)\b/<parameter>$1<\/parameter>/gu;
	return $str;
}

sub close_including($) {
	my ($tag) = @_;
	while (1) {
		my $element = pop @tagstack;
		last unless defined $element;
		print "  " x scalar(@tagstack), $element, "\n";
	}
}

sub close_upto($) {
	my ($tag) = @_;
	while (1) {
		my $element = pop @tagstack;
		last unless defined $element;
		if ($element eq $tag) {
			push @tagstack, $element;
			last;	
		}
		print "  " x scalar(@tagstack), $element, "\n";
	}
}

#
# Functions to process specific keywords
#

sub processnotimplemented($) {
	die("Sorry, no code has been implemented yet to handle the $curkeyword keyword"),
}

sub process_category($) {
	my ($cat) = @_;
	chomp($cat);

	my $ws = "  " x scalar(@tagstack);

	if ((not defined $curcat) or ($curcat ne $cat)) {
		# Start of a new category.

		# Finish up the old one (if there is one)
		close_including('</sect1>');

		# And start on the new one
		my $cat_id = "CATEGORY_" . $cat;
		$cat_id =~ s/\s+/_/gg;
		$cat_id =~ s/[^A-Za-z_]//gu;
		print $ws, "<sect1 id=\"$cat_id\">\n";
		print $ws, "  <title>", &quote_stuff ($cat), "</title>\n";
		push @tagstack, ('</sect1>');
	}
}

sub process_function($) {
	my ($func) = @_;

	my $ws = "  " x scalar(@tagstack);
	print $ws, "<refentry id=\"gnumeric-function-$func\">\n";
	print $ws, "  <refmeta>\n";
	print $ws, "    <refentrytitle><function>$func</function></refentrytitle>\n";
	print $ws, "  </refmeta>\n";
	print $ws, "  <refnamediv>\n";
	print $ws, "    <refname><function>$func</function></refname>\n";
	push @tagstack, ('</refentry>');
}

sub process_short_desc($) {
	my ($desc) = @_;

	my $ws = "  " x scalar(@tagstack);
	print $ws, "  <refpurpose>\n";
	print $ws, "    ", &markup_stuff ($desc), "\n";
	print $ws, "  </refpurpose>\n";
	print $ws, "</refnamediv>\n";
}

sub process_description($) {
	my ($text) = @_;
	my $ws = "  " x scalar(@tagstack);
	print $ws, "<refsect1>\n";
	print $ws, "  <title>Description</title>\n";
	my $haveparameters = 0;
	foreach my $l (split(/\n/, $text)) {
		if (!$haveparameters && $l =~ m/^\@\{/) {
			$haveparameters = 1;
		}
		print $ws,"    <para>", &markup_stuff($l), "</para>\n";
	}
	print $ws, "</refsect1>\n";
}

sub process_argumentdescription($) {
	my ($text) = @_;
	my $ws = "  " x scalar(@tagstack);
	print $ws, "<refsect1>\n";
	print $ws, "  <title>Arguments</title>\n";
	my $haveparameters = 0;
	foreach my $l (split(/\n/, $text)) {
		if (!$haveparameters && $l =~ m/^\@\{/) {
			$haveparameters = 1;
		}
		print $ws,"    <para>", &markup_stuff($l), "</para>\n";
	}
	print $ws, "</refsect1>\n";
}

sub process_note($) {
	my ($text) = @_;
	my $ws = "  " x scalar(@tagstack);
	print $ws, "<refsect1>\n";
	print $ws, "  <title>Note</title>\n";
	foreach my $l (split(/\n/, $text)) {
		print $ws,"    <para>", &markup_stuff($l), "</para>\n";
	}
	print $ws, "</refsect1>\n";
}

sub process_excel($) {
	my ($text) = @_;
	my $ws = "  " x scalar(@tagstack);
	print $ws, "<refsect1>\n";
	print $ws, "  <title>Microsoft Excel Compatibility</title>\n";
	foreach my $l (split(/\n/, $text)) {
		print $ws,"    <para>", &markup_stuff($l), "</para>\n";
	}
	print $ws, "</refsect1>\n";
}

sub process_odf($) {
	my ($text) = @_;
	my $ws = "  " x scalar(@tagstack);
	print $ws, "<refsect1>\n";
	print $ws, "  <title>OpenDocument Format (ODF) Compatibility</title>\n";
	foreach my $l (split(/\n/, $text)) {
		print $ws,"    <para>", &markup_stuff($l), "</para>\n";
	}
	print $ws, "</refsect1>\n";
}

sub process_syntax($) {
	my ($str) = @_;
	my $ws = "  " x scalar(@tagstack);
	$str = &markup_stuff ($str);
	$str =~ s/([\(\,])(\w*)/$1<parameter>$2<\/parameter>/gu;
	print $ws, "<refsynopsisdiv>\n";
	print $ws, "  <synopsis>$str</synopsis>\n";
	print $ws, "</refsynopsisdiv>\n";
}

sub process_examples($) {
	my ($text) = @_;
	my $ws = "  " x scalar(@tagstack);
	print $ws, "<refsect1>\n";
	print $ws, "   <title>Examples</title>\n";
	print $ws, "   <para>", &markup_stuff ($text), "</para>\n";
	push @tagstack, ('</refsect1>');
}

sub process_seealso($) {
	my ($text) = @_;

	my $linktxt = $text;
	$linktxt =~ s/\s//gu;
	$linktxt =~ s/\.$//u;
	my @links = split (/,/, $linktxt);

	my $ws = "  " x scalar(@tagstack);
	print $ws, "<refsect1>\n";
	print $ws, "  <title>See also</title>\n";
	my @a = ();
	print $ws, "  <para>\n";
	foreach my $link (@links) {
	    push @a, $ws . "    <link linkend=\"gnumeric-function-$link\"><function>$link</function></link>";
	}
	if (scalar(@a) > 0) {
	    print join (",\n", @a), ".\n";
	}
	print $ws, "  </para>\n";
	push @tagstack, ('</refsect1>');
}

my %processor = (
	'CATEGORY'	=> \&process_category,
	'FUNCTION'	=> \&process_function,
	'SHORTDESC'	=> \&process_short_desc,
	'SYNTAX'	=> \&process_syntax,
	'ARGUMENTDESCRIPTION'	=> \&process_argumentdescription,
	'DESCRIPTION'	=> \&process_description,
	'SEEALSO'	=> \&process_seealso,
	'NOTE'		=> \&process_note,
	'EXCEL'		=> \&process_excel,
	'ODF'		=> \&process_odf,
);

sub process_chunk(@) {
	my (@chunk) = @_;
	return unless scalar(@chunk) > 0;

	# Trim off any trailing empty lines
	while (scalar(@chunk) > 0) {
		last unless $chunk[$#chunk] =~ /^\s*$/;
		pop @chunk;
	}
	
	my $cat;
	my $in_description = 0;

	$curkeyword = undef;
	my $lines = "";
	for my $i (0..$#chunk) {
		my $chunk = $chunk[$i];
		chomp $chunk;

		if ($chunk =~ m/^\@(\w+)=(.*)/) {
			my ($key, $val) = (uc($1), $2);

			$cat = $val if ($key eq 'CATEGORY');
			$curfunc = $val if ($key eq 'FUNCTION');

			if (defined($processor{$key})) {
				if (defined($curkeyword)) {
					# Process the previous tag for which
					# all lines have been gathered now.
					&{$processor{$curkeyword}}($lines);
				}
				$curkeyword = $key;
				$lines = $val;
				next;
			} else {
				die("Unrecognised keyword: $key\n");
			}
		}
		$lines .= "\n" . $chunk;
	}
	&{$processor{$curkeyword}}($lines);

	$curcat = $cat;

	close_upto('</sect1>'); # Closing tag of a category.
}

sub main() {
	my $line;
	my @chunk = ();
	while ($line = <>) {
		if ($line =~ m/^\@CATEGORY=/) {
			# We're at the start of a new chunk of function
			# documentation
			process_chunk(@chunk);
			print "\n";
			@chunk = ($line);
		} else {
			push @chunk, $line;
		}
	}
	process_chunk(@chunk);
	while (my $el = pop @tagstack) {
		print "  " x scalar(@tagstack), $el, "\n";
	}
}

main();
exit(0);

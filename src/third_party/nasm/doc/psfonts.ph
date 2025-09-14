#!/usr/bin/perl
#
# Font metrics for the PS code generator
#

# Font substitution lists, in order of preference

# Note: for some reason the Source Pro fonts use -It rather than
# -Italic for the PostScript name of the OTF font, but some systems
# have been said to want it the other way, maybe because they have the
# TTF format installed?
#
# Thus, support various aliases in combination.

sub font {
    my($fonts, @mods) = @_;
    my @f = map { $_.'-' } @$fonts;

    foreach my $m (@mods) {
	@f = map { my $ff = $_; map { $ff.$_ } @$m } @f;
    }

    return map { /^(.*?)-*$/; $1 } @f;
}

my $text    = ['SourceSans', 'SourceSans3',
	       'SourceSansPro', 'SourceSansPro3',
	       'LiberationSans', 'Arial', 'Helvetica'];
my $code    = ['SourceCodePro', 'LiberationMono', 'Courier'];
my $regular = ['Regular', ''];
my $italic  = ['Italic', 'It'];
my $bold    = ['Bold'];
my $semi    = ['Semibold', 'Bold'];

my @TText = font($text, $bold);
my @TItal = font($text, $bold, $italic);
my @TCode = font($code, $bold);
my @HText = font($text, $semi);
my @HItal = font($text, $semi, $italic);
my @HCode = font($code, $semi);
my @BText = font($text, $regular);
my @BItal = font($text, $italic);
my @BCode = font($code, $regular);
my @QText = font($text, $italic);
my @QBold = font($text, $bold, $italic);
my @QCode = font($code, $regular);
my @XCode = font($code, $regular);

# The fonts we want to use for various things
# The order is: <normal> <emphatic> <code>

my $lf = 1.2;			# Leading scale factor
my $cf = 0.8;			# Code size scale factor

my $st = 20;
%TitlFont = (name => 'tfont',
	     leading => $st*$lf,
	     fonts => [[$st, \@TText], [$st, \@TItal], [$st*$cf, \@TCode]]);

my $sc = 18;
%ChapFont = (name => 'cfont',
	     leading => $sc*$lf,
	     fonts => [[$sc, \@HText], [$sc, \@HItal], [$sc*$cf, \@HCode]]);

my $sh = 14;
%HeadFont = (name => 'hfont',
		leading => $sh*$lf,
		fonts => [[$sh, \@HText], [$sh, \@HItal], [$sh*$cf, \@HCode]]);

my $ss = 12;
%SubhFont = (name => 'sfont',
	     leading => $ss*$lf,
	     fonts => [[$ss, \@HText], [$ss, \@HItal], [$ss*$cf, \@HCode]]);

my $sb = 10;
%BodyFont = (name => 'bfont',
	     leading => $sb*$lf,
	     fonts => [[$sb, \@BText], [$sb, \@BItal], [$sb*$cf, \@BCode]]);

my $sq = 9;
%BquoFont = (name => 'qfont',
	     leading => $sq*$lf,
	     fonts => [[$sq, \@QText], [$sq, \@QBold], [$sq*$cf, \@QCode]]);

my $sx = $sb*$cf;
%CodeFont = (name => 'xfont',
	     leading => $sx*$lf,
	     fonts => [[$sx, \@XCode], [$sx, \@XCode], [$sx, \@XCode]]);

#
# List of all fontsets; used to compute the list of fonts needed
#
@AllFonts = ( \%TitlFont, \%ChapFont, \%HeadFont, \%SubhFont, \%BodyFont,
    \%BquoFont, \%CodeFont );

# OK
1;

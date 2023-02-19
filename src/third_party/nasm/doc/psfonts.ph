#!/usr/bin/perl
#
# Font metrics for the PS code generator
#

# Font substitution lists, in order of preference
my @TText = ('SourceSansPro-Bold', 'ClearSans-Bold', 'LiberationSans-Bold',
	     'Arial-Bold', 'Helvetica-Bold');
my @TItal = ('SourceSansPro-BoldIt', 'ClearSans-BoldItalic', 'LiberationSans-BoldItalic',
	     'Arial-BoldItalic', 'Helvetica-BoldItalic');
my @TCode = ('SourceCodePro-Bold', 'LiberationMono-Bold', 'Courier-Bold');
my @HText = ('SourceSansPro-Semibold', 'ClearSans-Bold', 'Arial-Bold', 'Helvetica-Bold');
my @HItal = ('SourceSansPro-SemiboldIt', 'ClearSans-BoldItalic',
	     'Arial-BoldItalic', 'Helvetica-BoldItalic');
my @HCode = ('SourceCodePro-Semibold', 'LiberationMono-Bold', 'Courier-Bold');
my @BText = ('SourceSansPro-Regular', 'ClearSans', 'LiberationSans', 'Arial', 'Helvetica');
my @BItal = ('SourceSansPro-It', 'ClearSans-Italic', 'LiberationSans-Italic',
	     'Arial-Italic', 'Helvetica-Italic');
my @BCode = ('SourceCodePro-Regular', 'LiberationMono', 'Courier');
my @QText = ('SourceSansPro-It', 'ClearSans-Italic', 'LiberationSans-Italic',
	     'Arial-Italic', 'Helvetica-Italic');
my @QBold = ('SourceSansPro-BoldIt', 'ClearSans-BoldItalic', 'LiberationSans-BoldItalic', 'Arial-Bold', 'Helvetica-BoldItalic');
my @QCode = ('SourceCodePro-Regular', 'LiberationMono', 'Courier');
my @XCode = ('SourceCodePro-Regular', 'LiberationMono', 'Courier');

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

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

# The fonts we want to use for various things
# The order is: <normal> <emphatic> <code>

%TitlFont = (name => 'tfont',
	     leading => 24,
	     fonts => [[20, \@TText], [20, \@TItal], [20, \@TCode]]);
%ChapFont = (name => 'cfont',
	     leading => 21.6,
	     fonts => [[18, \@HText], [18, \@HItal], [18, \@HCode]]);
%HeadFont = (name => 'hfont',
		leading => 16.8,
		fonts => [[14, \@HText], [14, \@HItal], [14, \@HCode]]);
%SubhFont = (name => 'sfont',
	     leading => 14.4,
	     fonts => [[12, \@HText], [12, \@HItal], [12, \@HCode]]);
%BodyFont = (name => 'bfont',
	     leading => 12,
	     fonts => [[10, \@BText], [10, \@BItal], [10, \@BCode]]);
%BquoFont = (name => 'qfont',
	     leading => 10.8,
	     fonts => [[9, \@QText], [9, \@QBold], [9, \@QCode]]);
#
# List of all fontsets; used to compute the list of fonts needed
#
@AllFonts = ( \%TitlFont, \%ChapFont, \%HeadFont, \%SubhFont, \%BodyFont,
    \%BquoFont);

# OK
1;

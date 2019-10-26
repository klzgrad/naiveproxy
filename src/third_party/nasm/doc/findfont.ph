#!/usr/bin/perl
## --------------------------------------------------------------------------
##
##   Copyright 1996-2017 The NASM Authors - All Rights Reserved
##   See the file AUTHORS included with the NASM distribution for
##   the specific copyright holders.
##
##   Redistribution and use in source and binary forms, with or without
##   modification, are permitted provided that the following
##   conditions are met:
##
##   * Redistributions of source code must retain the above copyright
##     notice, this list of conditions and the following disclaimer.
##   * Redistributions in binary form must reproduce the above
##     copyright notice, this list of conditions and the following
##     disclaimer in the documentation and/or other materials provided
##     with the distribution.
##
##     THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
##     CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
##     INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
##     MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
##     DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
##     CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
##     SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
##     NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
##     LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
##     HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
##     CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
##     OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
##     EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
##
## --------------------------------------------------------------------------

#
# Try our best to find a specific PostScipt font in the system.
# We need to find the font files so we can extract the metrics.
# Sadly there isn't any reasonable Perl module to do this for us,
# as far as I can tell.
#

use strict;
use File::Spec;
use File::Find;

require 'afmmetrics.ph';
require 'ttfmetrics.ph';

my %font_info_hash = ();
my $fonts_scanned = 0;
my %prefs = { 'otf' => 1, 'ttf' => 2, 'pfa' => 3, 'pfb' => 4 };

sub add_file_to_font_hash($) {
    my($filename) = @_;

    return unless ( -f $filename );
    return unless ( $filename =~ /^(.*)\.([[:alnum:]]+)$/ );

    my $filestem = $1;
    my $fonttype = $2;
    my $fontdata;

    if ( $filename =~ /\.(otf|ttf)$/i ) {
        $fontdata = parse_ttf_file($filename);
    } elsif ( $filename =~ /\.(pfa|pfb)$/i ) {
        if ( -f "${filestem}.afm" ) {
            $fontdata = parse_afm_file($filestem, $fonttype);
        }
    }

    return unless (defined($fontdata));

    my $oldinfo = $font_info_hash{$fontdata->{name}};

    if (!defined($oldinfo) ||
        $prefs{$fontdata->{type}} < $prefs{$oldinfo->{type}}) {
        $font_info_hash{$fontdata->{name}} = $fontdata;
    }
}

my $win32_ok = eval {
    require Win32::TieRegistry;
    Win32::TieRegistry->import();
    1;
};

# Based on Font::TTF::Win32 by
# Martin Hosken <http://scripts.sil.org/FontUtils>.
# LICENSING
#
#   Copyright (c) 1998-2014, SIL International (http://www.sil.org)
#
#   This module is released under the terms of the Artistic License 2.0.
#   For details, see the full text of the license in the file LICENSE.
sub scanfonts_win32() {
    return unless ($win32_ok);

    my $Reg = $::Registry->Open('', {Access=>'KEY_READ', Delimiter=>'/'});
    my $fd;
    foreach my $win ('Windows NT', 'Windows') {
	$fd = $Reg->{"HKEY_LOCAL_MACHINE/SOFTWARE/Microsoft/$win/CurrentVersion/Fonts"};
        last if (defined($fd));
    }
    return unless (defined($fd));

    foreach my $font (keys(%$fd)) {
        my($fname, $ftype) = ($font =~ m:^/(.+?)(| \([^\(\)]+\))$:);
        next unless ($ftype =~ / \((TrueType|OpenType)\)$/);
        my $file = File::Spec->rel2abs($fd->{$font}, $ENV{'windir'}.'\\fonts');
        add_file_to_font_hash($file);
    }
}

sub font_search_file {
    add_file_to_font_hash($_);
}

sub findfont($) {
    my($fontname) = @_;
    my $win32 = eval {
	require Font::TTF::Win32;
	Font::TTF::Win32->import();
	1;
    };
    my($file, $psname, $fontdata);

    if (exists($font_info_hash{$fontname})) {
	return $font_info_hash{$fontname};
    }

    # Are we on a system that uses fontconfig?
    # NOTE: use a single string for the command here, or this
    # script dies horribly on Windows, even though this isn't really
    # applicable there...
    if (open(my $fh, '-|',
	     "fc-match -f \"%{file}\\n%{postscriptname}\\n\" ".
	     "\" : postscriptname=$fontname\"")) {
        chomp($file = <$fh>);
        chomp($psname = <$fh>);
        close($fh);
        if ( -f $file ) {
            if ($psname eq $fontname) {
                add_file_to_font_hash($file);
            }
            if (!exists($font_info_hash{$fontname})) {
                $font_info_hash{$fontname} = undef;
            }
            return $font_info_hash{$fontname};
        }
    }

    if (exists($font_info_hash{$fontname})) {
        return $font_info_hash{$fontname};
    } elsif ($fonts_scanned >= 1) {
        return $font_info_hash{$fontname} = undef;
    }

    scanfonts_win32();
    $fonts_scanned = 1;

    if (exists($font_info_hash{$fontname})) {
        return $font_info_hash{$fontname};
    } elsif ($fonts_scanned >= 2) {
        return $font_info_hash{$fontname} = undef;
    }

    # Search a set of possible locations for a file, from a few different
    # systems...
    my @dirs = ('fonts', '/usr/share/fonts', '/usr/lib/fonts', '/Library/Fonts');
    push @dirs, $ENV{'windir'}.'\\fonts' if (defined $ENV{'windir'});
    push @dirs, $ENV{'HOME'}.'/.fonts', $ENV{'HOME'}.'/Library/Fonts'
	if (defined $ENV{'HOME'});

    find({wanted => \&font_search_file, follow=>1, no_chdir=>1}, @dirs);
    $fonts_scanned = 2;

    return $font_info_hash{$fontname};
}

1;

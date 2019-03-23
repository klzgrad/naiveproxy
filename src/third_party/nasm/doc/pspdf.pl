#!/usr/bin/perl
#
# Wrapper around a variety of programs that can do PS -> PDF conversion
#

use strict;
use File::Spec;

my $compress = 1;

my $win32_ok = eval {
    require Win32::TieRegistry;
    Win32::TieRegistry->import();
    1;
};

while ($ARGV[0] =~ /^-(.*)$/) {
    my $opt = $1;
    shift @ARGV;

    if ($opt eq '-nocompress') {
        $compress = 0;
    }
}

# Ghostscript executable name.  "gs" on Unix-based systems.
my $gs = 'gs';

my ($in, $out) = @ARGV;

if (!defined($out)) {
    die "Usage: $0 [-nocompress] infile ou{ tfile\n";
}

# If Win32, help GhostScript out with some defaults
sub win32_gs_help() {
    return if (!$win32_ok);

    use Sort::Versions;
    use sort 'stable';

    my $Reg = $::Registry->Open('', {Access => 'KEY_READ', Delimiter => '/'});
    my $dir;
    my @gs;

    foreach my $k1 ('HKEY_CURRENT_USER/Software/',
                    'HKEY_LOCAL_MACHINE/SOFTWARE/') {
        foreach my $k2 ('Artifex/', '') {
            foreach my $k3 ('GPL Ghostscript/', 'AFPL Ghostscript/',
                            'Ghostscript/') {
                my $r = $Reg->{$k1.$k2.$k3};
                if (ref($r) eq 'Win32::TieRegistry') {
                    foreach my $k (keys(%$r)) {
                        my $rk = $r->{$k};
                        if (ref($rk) eq 'Win32::TieRegistry' &&
                            defined($rk->{'/GS_LIB'})) {
                            push @gs, $rk;
                        }
                    }
                }
            }
        }
    }

    @gs = sort {
        my($av) = $a->Path =~ m:^.*/([^/]+)/$:;
        my($bv) = $b->Path =~ m:^.*/([^/]+)/$:;
        versioncmp($av, $bv);
    } @gs;

    return unless (scalar(@gs));

    $ENV{'PATH'} .= ';' . $gs[0]->{'/GS_LIB'};
    $ENV{'GS_FONTPATH'} .= (defined($ENV{'GS_FONTPATH'}) ? ';' : '')
        . $ENV{'windir'}.'\\fonts';

    my $gsp = undef;
    foreach my $p (split(/\;/, $gs[0]->{'/GS_LIB'})) {
	foreach my $exe ('gswin64c.exe', 'gswin32c.exe', 'gs.exe') {
	    last if (defined($gsp));
	    my $e = File::Spec->catpath($p, $exe);
	    $gsp = $e if (-f $e && -x _);
	}
    }

    $gs = $gsp if (defined($gsp));
}

# Remove output file
unlink($out);

# 1. Acrobat distiller
my $r = system('acrodist', '-n', '-q', '--nosecurity', '-o', $out, $in);
exit 0 if ( !$r && -f $out );

# 2. ps2pdf (from Ghostscript)
#
# GhostScript uses # rather than = to separate options and values on Windows,
# it seems.  Call gs directly rather than ps2pdf, because -dSAFER
# breaks font discovery on some systems, apparently.
win32_gs_help();
my $o = $win32_ok ? '#' : '=';
my $r = system($gs, "-dCompatibilityLevel${o}1.4", "-q",
	       "-P-", "-dNOPAUSE", "-dBATCH", "-sDEVICE${o}pdfwrite",
	       "-sstdout${o}%stderr", "-sOutputFile${o}${out}",
	       "-dOptimize${o}true", "-dEmbedAllFonts${o}true",
               "-dCompressPages${o}" . ($compress ? 'true' : 'false'),
               "-dUseFlateCompression${o}true",
	       "-c", ".setpdfwrite", "-f", $in);
exit 0 if ( !$r && -f $out );

# 3. pstopdf (BSD/MacOS X utility)
my $r = system('pstopdf', $in, '-o', $out);
exit 0 if ( !$r && -f $out );

# Otherwise, fail
unlink($out);
exit 1;

#!/usr/bin/perl
#Perform tests on nasm

use strict;
use warnings;

use Getopt::Long qw(GetOptions);
use Pod::Usage qw(pod2usage);

use File::Basename qw(fileparse);
use File::Compare qw(compare compare_text);
use File::Copy qw(move);
use File::Path qw(mkpath rmtree);

#sub debugprint { print (pop() . "\n"); }
 sub debugprint { }

my $globalresult = 0;

#Process one testfile
sub perform {
    my ($clean, $diff, $golden, $nasm, $quiet, $testpath) = @_;
    my ($stdoutfile, $stderrfile) = ("stdout", "stderr");

    my ($testname, $ignoredpath, $ignoredsuffix) = fileparse($testpath, ".asm");
    debugprint $testname;

    my $outputdir = $golden ? "golden" : "testresults";

    mkdir "$outputdir" unless -d "$outputdir";

    if ($clean) {
        rmtree "$outputdir/$testname";
        return;
    }

    if(-d "$outputdir/$testname") {
        rmtree "$outputdir/$testname";
    }

    open(TESTFILE, '<', $testpath) or (warn "Can't open $testpath\n", return);
    TEST:
    while(<TESTFILE>) {
        #See if there is a test case
        last unless /Testname=(.*);\s*Arguments=(.*);\s*Files=(.*)/;
        my ($subname, $arguments, $files) = ($1, $2, $3);
        debugprint("$subname | $arguments | $files");

        #Call nasm with this test case
        system("$nasm $arguments $testpath > $stdoutfile 2> $stderrfile");
        debugprint("$nasm $arguments $testpath > $stdoutfile 2> $stderrfile ----> $?");

        #Move the output to the test dir
        mkpath("$outputdir/$testname/$subname");
        foreach(split / /,$files) {
            if (-f $_) {
                move($_, "$outputdir/$testname/$subname/$_") or die $!
            }
        }
        unlink ("$stdoutfile", "$stderrfile"); #Just to be sure

        if($golden) {
            print "Test $testname/$subname created.\n" unless $quiet;
        } else {
            #Compare them with the golden files
            my $result = 0;
            my @failedfiles = ();
            foreach(split / /, $files) {
                if(-f "$outputdir/$testname/$subname/$_") {
                    my $temp;
                    if($_ eq $stdoutfile or $_ eq $stderrfile) {
                        #Compare stdout and stderr in text mode so line ending changes won't matter
                        $temp = compare_text("$outputdir/$testname/$subname/$_", "golden/$testname/$subname/$_",
                                             sub { my ($a, $b) = @_;
                                                   $a =~ s/\r//g;
                                                   $b =~ s/\r//g;
                                                   $a ne $b; } );
                    } else {
                        $temp = compare("$outputdir/$testname/$subname/$_", "golden/$testname/$subname/$_");
                    }

                    if($temp == 1) {
                        #different
                        $result = 1;
                        $globalresult = 1;
                        push @failedfiles, $_;
                    } elsif($temp == -1) {
                        #error
                        print "Can't compare at $testname/$subname file $_\n";
                        next TEST;
                    }
                } elsif (-f "golden/$testname/$subname/$_") {
                    #File exists in golden but not in output
                    $result = 1;
                    $globalresult = 1;
                    push @failedfiles, $_;
                }
            }

            if($result == 0) {
                print "Test $testname/$subname succeeded.\n" unless $quiet;
            } elsif ($result == 1) {
                print "Test $testname/$subname failed on @failedfiles.\n";
                if($diff) {
                    for(@failedfiles) {
                        if($_ eq $stdoutfile or $_ eq $stderrfile) {
                            system "diff -u golden/$testname/$subname/$_ $outputdir/$testname/$subname/$_";
                            print "\n";
                        }
                    }
                }
            } else {
                die "Impossible result";
            }
        }
    }
    close(TESTFILE);
}

my $nasm;
my $clean = 0;
my $diff = 0;
my $golden = 0;
my $help = 0;
my $verbose = 0;

GetOptions('clean' => \$clean,
           'diff'=> \$diff,
           'golden' => \$golden,
           'help' => \$help,
           'verbose' => \$verbose,
           'nasm=s' => \$nasm
          ) or pod2usage();

pod2usage() if $help;
die "Please specify either --nasm or --clean. Use --help for help.\n"
unless $nasm or $clean;
die "Please specify the test files, e.g. *.asm\n" unless @ARGV;

unless (!defined $nasm or -x $nasm) {
  warn "Warning: $nasm may not be executable. Expect problems.\n\n";
  sleep 5;
}

perform($clean, $diff, $golden, $nasm, ! $verbose, $_) foreach @ARGV;
exit $globalresult;

__END__

=head1 NAME

performtest.pl - NASM regression tester based on golden files

=head1 SYNOPSIS

performtest.pl [options] [testfile.asm ...]

Runs NASM on the specified test files and compare the results
with "golden" output files.

 Options:
     --clean     Clean up test results (or golden files with --golden)
     --diff      Execute diff when stdout or stderr don't match
     --golden    Create golden files
     --help      Get this help
     --nasm=file Specify the file name for the NASM executable, e.g. ../nasm
     --verbose   Get more output

     If --clean is not specified, --nasm is required.

 testfile.asm ...:
    One or more files that NASM should be tested with,
    often *.asm in the test directory.
    It should contain one or more option lines at the start,
    in the following format:

;Testname=<testname>; Arguments=<arguments to nasm>; Files=<output files>

    If no such lines are found at the start, the file is skipped.
    testname should ideally describe the arguments, eg. unoptimized for -O0.
    arguments can be an optimization level (-O), an output format (-f),
    an output file specifier (-o) etc.
    The output files should be a space separated list of files that will
    be checked for regressions. This should often be the output file
    and the special files stdout and stderr.

Any mismatch could be a regression,
but it doesn't have to be. COFF files have a timestamp which
makes this method useless. ELF files have a comment section
with the current version of NASM, so they will change each version number.

=cut

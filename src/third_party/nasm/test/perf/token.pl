#!/usr/bin/perl
#
# Generate a test case for token lookup performance
#

@insns = qw(add sub adc sbb and or xor mov);
@regs  = qw(eax ebx ecx edx esp ebp esi edi);

srand(0);
sub pickone(@) {
    return $_[int(rand(scalar @_))];
}

($len) = @ARGV;
$len = 1000000 unless ($len);

print "\tbits 32\n";
print "\n";

for ($i = 0; $i < $len; $i++) {
    print "\t", pickone(@insns), " ",
        pickone(@regs), ",", pickone(@regs), "\n";
}

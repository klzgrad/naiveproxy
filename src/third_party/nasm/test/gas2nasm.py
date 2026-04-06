#!/usr/bin/env python -tt
# -*- python -*-
# Convert gas testsuite file to NASM test asm file
# usage >
# python gas2nasm.py -i input_gas_file -o output_nasm_file -b bits
# e.g. python gas2nasm.py -i x86-64-avx512f-intel.d -o avx512f.asm -b 64

import sys
import os
import optparse
import re

def setup():
    parser = optparse.OptionParser()
    parser.add_option('-i', dest='input', action='store',
            default="",
            help='Name for input gas testsuite file.')
    parser.add_option('-o', dest='output', action='store',
            default="",
            help='Name for output NASM test asm file.')
    parser.add_option('-b', dest='bits', action='store',
            default="",
            help='Bits for output ASM file.')
    parser.add_option('-r', dest='raw_output', action='store',
            default="",
            help='Name for raw output bytes in text')
    (options, args) =  parser.parse_args()
    return options

def read(options):
    with open(options.input, 'rb') as f:
        recs = []
        for line in f:
            if line[0] == '[':
                d = []
                strr = line[16:].partition('   ')
                if strr[1] == '':
                    strr = line[16:].partition('\t')
                l = strr[0].strip()
                r = strr[2].strip()
                d.append(l)
                d.append(r)
                recs.append(d)
    return recs

def commas(recs):
    replace_tbl = {' PTR':'', '\\':'', 'MM':'', 'XWORD':'OWORD'}
    reccommas = []
    for insn in recs:
        new = []
        byte = '0x' + insn[0].replace(' ', ', 0x')
        for rep in replace_tbl.keys():
            insn[1] = insn[1].replace(rep, replace_tbl[rep])
        mnemonic = insn[1]

        # gas size specifier for gather and scatter insturctions seems wrong. just remove them.
        if 'gather' in insn[1] or 'scatter' in insn[1]:
            mnemonic = mnemonic.replace('ZWORD', '')

        new.append(byte)
        new.append(mnemonic)
        reccommas.append(new)
    return reccommas

# The spaces reserved here can be adjusted according to the output string length.
# maxlen printed out at the end of the process will give a hint for it.
outstrfmt = "testcase\t{ %-70s }, { %-60s }\n"

macro = "%macro testcase 2\n %ifdef BIN\n  db %1\n %endif\n %ifdef SRC\n  %2\n %endif\n%endmacro\n\n\n"

def write(data, options):
    if options.output:
        with open(options.output, 'wb') as out:
            out.write(macro)
            if options.bits:
                out.write('bits ' + options.bits + '\n\n')
            for insn in data:
                outstr = outstrfmt % tuple(insn)
                out.write(outstr)

def write_rawbytes(data, options):
    if options.raw_output:
        with open(options.raw_output, 'wb') as out:
            for insn in data:
                out.write(insn[0] + '\n')

if __name__ == "__main__":
    options = setup()
    recs = read(options)

    write_rawbytes(recs, options)

    recs = commas(recs)

    write(recs, options)

    maxlen = [0,0,0,0,0,0,0,0]
    for insn in recs:
#print insn[0] + '\t<-\t' + insn[1]
        print outstrfmt[:-1] % tuple(insn)
        for i, strstr in enumerate(insn):
            if maxlen[i] < len(strstr): maxlen[i] = len(strstr)

    print maxlen

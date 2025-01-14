#! /bin/sh

[ $1 ] || {
    echo "Usage: $0 <library name> <module> [...]"
    exit 1
}

libname=$1; shift

rdflib c $libname

for f in $*; do
	rdflib a $libname $f $f
done

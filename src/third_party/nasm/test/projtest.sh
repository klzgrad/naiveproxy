#!/bin/bash -

there="$(realpath "$(dirname "$0")")"

[ -z "$NASM1" ] && NASM1=nasm
NASM1=$(which "$NASM1" 2>/dev/null)
if [ -z "$NASM1" ]; then
	echo 'Please install a reference nasm ...' 1>&2
	exit 1
fi

NASM2="$1"
[ -z "$NASM2" ] && NASM2=../nasm
NASM2=$(which "$NASM2" 2>/dev/null)
if [ -z "$NASM2" ]; then
    echo 'Test nasm not found' 1>&2
    exit 1
fi

PROJ="$2"
PROJ_GET_BUILD="get_build_${PROJ}.sh"
if ! [ -f ${PROJ_GET_BUILD} ]; then
	echo 'No knowledge in building the project' 1>&2
	exit 1
fi

export PATH=${PWD}:$PATH

set -x

mkdir -p "${there}/${PROJ}"
cd "${there}/${PROJ}"
here="$(pwd)"

logfile="$here/test.log"
filelist="$here/file.list"
rm -f "$logfile"
export projnasm_logfile="$logfile"
export projnasm_filelist="$filelist"
export projnasm_nasm1="$NASM1"
export projnasm_nasm2="$NASM2"

source ../${PROJ_GET_BUILD}
rev=$?
if [ "$rev" -ne "0" ]; then
	echo ${PROJ} compiling failed ...
	exit $rev
fi

set +x
tmpf=$(mktemp)

{
for y in "o" "obj"
do
for x in $(grep -o -P "\-o .*\.${y}" $logfile | sed -e 's/-o //')
do
	f=$x
	if ! [ -f $x ]; then
		b=$(basename $x)
		if find -name $b >/dev/null; then
			find -name $b >$tmpf
			while read -r line; do
				if [[ "$line" == *"$x" ]]; then
					f=$line
				fi
			done < "$tmpf"
		fi
	fi
	if ! [ -f $f ]; then
		# probably it's a temporary assembly being tested
		continue
	fi
	if ! [ -f ${f}.1 ]; then
		echo file ${f}.1 does not exist
	fi

	if ! [ -f ${f}.2 ]; then
		echo file ${f}.2 does not exist
	fi

	objdump -d ${f}.1 | tail -n +4 >/tmp/1.dump
	objdump -d ${f}.2 | tail -n +4 >/tmp/2.dump
	if ! diff /tmp/1.dump /tmp/2.dump >/dev/null; then
		echo [differs] $f
		#diff -u /tmp/1.dump /tmp/2.dump
	else
		echo [matches] $f
	fi
	rm -f /tmp/1.dump /tmp/2.dump
done
done
} | tee "$here/results"

rm -f $tmpf
rev=$(! grep -e " does not exist" -e "\[differs\]" $here/results >/dev/null)

exit $rev

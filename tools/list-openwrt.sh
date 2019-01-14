#!/bin/sh
# $version can be 21.02 or 19.07.
version=19.07.7
if [ ! -d /tmp/openwrt ]; then
  cd /tmp
  git clone https://github.com/openwrt/openwrt.git
  cd openwrt
fi
cd /tmp/openwrt
git -c advice.detachedHead=false checkout v$version
export TOPDIR=$PWD
cd target/linux
>targets.git
for target in *; do
  [ -d $target ] || continue
  subtargets=$(make -C $target --no-print-directory DUMP=1 TARGET_BUILD=1 val.SUBTARGETS 2>/dev/null)
  [ "$subtargets" ] || subtargets=generic
  for subtarget in $subtargets; do
    echo $(make -C $target --no-print-directory DUMP=1 TARGET_BUILD=1 SUBTARGET=$subtarget 2>/dev/null | egrep '^(Target:|Target-Arch-Packages:)' | cut -d: -f2) >>targets.git
  done
done

targets=$(curl -s https://downloads.openwrt.org/releases/$version/targets/ | grep '<td class="n"><a href=' | cut -d'"' -f4 | sed 's,/,,')
>targets.sdk
for target in $targets; do
  subtargets=$(curl -s https://downloads.openwrt.org/releases/$version/targets/$target/ | grep '<td class="n"><a href=' | cut -d'"' -f4 | sed 's,/,,')
  for subtarget in $subtargets; do
    arch=$(curl -s https://downloads.openwrt.org/releases/$version/targets/$target/$subtarget/profiles.json | grep arch_packages | cut -d'"' -f4)
    echo $target/$subtarget $arch >>targets.sdk
  done
done

cat >parse-targets.py <<EOF
arch_by_target_git = {}
arch_by_target_sdk = {}
for line in open('targets.git'):
    fields = line.split()
    if not fields:
        continue
    arch_by_target_git[fields[0]] = fields[1]
for line in open('targets.sdk'):
    fields = line.split()
    if len(fields) == 2:
        if arch_by_target_git[fields[0]] != fields[1]:
            raise Exception(line + ': wrong arch')
        arch_by_target_sdk[fields[0]] = fields[1]
    else:
        arch_by_target_sdk[fields[0]] = ''
for arch in sorted(set(arch_by_target_git.values())):
    targets = []
    for t in arch_by_target_git:
        if arch_by_target_git[t] != arch:
            continue
        if t in arch_by_target_sdk:
            targets.append(t)
        else:
            targets.append('~~' + t + '~~')
    print('|', arch, '|?|', ' '.join(sorted(set(targets))), '|')
EOF
python3 parse-targets.py

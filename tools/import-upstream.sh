#!/bin/sh
set -ex
have_version=$(cut -d= -f2 src/chrome/VERSION | tr '\n' . | cut -d. -f1-4)
want_version=$(cat CHROMIUM_VERSION)
if [ "$have_version" = "$want_version" ]; then
  exit 0
fi
name="chromium-$want_version"
tarball="$name.tar.xz"
url="https://commondatastorage.googleapis.com/chromium-browser-official/$tarball"
root=$(git rev-list --max-parents=0 HEAD)
git config core.autocrlf false
git config core.safecrlf false
git -c advice.detachedHead=false checkout $root
rm -rf src
git checkout master -- tools
sed -i "s/^\^/$name\//" tools/include.txt
curl "$url" -o- | tar xJf - --wildcards --wildcards-match-slash -T tools/include.txt
mv "$name" src
find src -name .gitignore -delete
git rm --quiet --force -r tools
git add src
git commit --quiet --amend -m "Import $name" --date=now
git rebase --onto HEAD HEAD master

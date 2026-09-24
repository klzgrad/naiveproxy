#!/usr/bin/env bash

set -e # Fail on error
set -u # Treat unset variables as an error and exit immediately

ACTION='\033[1;90m'
FINISHED='\033[1;96m'
NOCOLOR='\033[0m'
ERROR='\033[0;31m'

echo -e "${ACTION}Checking environment${NOCOLOR}"
if [[ ! $1 =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]
then
    echo -e "${ERROR}Invalid version number. Aborting. ${NOCOLOR}"
    exit 1
fi

declare -r VERSION=$1
declare -r GIT_TAG="v$1"

BRANCH=$(git rev-parse --abbrev-ref HEAD)
if [[ "${BRANCH}" != "main" ]]
then
    echo -e "${ERROR}Not on main. Aborting. ${NOCOLOR}"
    echo
    exit 1
fi

git fetch
HEADHASH=$(git rev-parse HEAD)
UPSTREAMHASH=$(git rev-parse main@{upstream})

if [[ "${HEADHASH}" != "${UPSTREAMHASH}" ]]
then
    echo -e "${ERROR}Not up to date with origin. Aborting.${NOCOLOR}"
    echo
    exit 1
fi

git update-index -q --refresh
if ! git diff-index --quiet HEAD --
then
    echo -e "${ERROR}Branch has uncommitted changes. Aborting.${NOCOLOR}"
    exit 1
fi

if [ ! -z "$(git ls-files --exclude-standard --others)" ]
then
    echo -e "${ERROR}Branch has untracked files. Aborting.${NOCOLOR}"
    exit 1
fi

declare -r LATEST_GIT_TAG=$(git describe --tags --abbrev=0)
declare -r LATEST_VERSION=${LATEST_GIT_TAG#"v"}

if ! dpkg --compare-versions "${VERSION}" "gt" "${LATEST_VERSION}"
then
    echo -e "${ERROR}Invalid version ${VERSION} <= ${LATEST_VERSION} (latest). Aborting.${NOCOLOR}"
    exit 1
fi

echo -e "${ACTION}Modifying CMakeLists.txt${NOCOLOR}"
sed -i "s/CpuFeatures VERSION ${LATEST_VERSION}/CpuFeatures VERSION ${VERSION}/g" CMakeLists.txt

echo -e "${ACTION}Modifying MODULE.bazel${NOCOLOR}"
sed -i "s/CPU_FEATURES_VERSION = \"${LATEST_VERSION}\"/CPU_FEATURES_VERSION = \"${VERSION}\"/g" MODULE.bazel

echo -e "${ACTION}Modifying build.zig.zon${NOCOLOR}" 
sed -i "s/.version = \"${LATEST_VERSION}\"/.version = \"${VERSION}\"/g" build.zig.zon 

echo -e "${ACTION}Commit new revision${NOCOLOR}"
git add CMakeLists.txt MODULE.bazel
git commit -m"Release ${GIT_TAG}"

echo -e "${ACTION}Check for unstaged or uncommited changes${NOCOLOR}"
git diff --quiet && git diff --quiet --cached

echo -e "${ACTION}Create new tag${NOCOLOR}"
git tag ${GIT_TAG}

echo -e "${FINISHED}Manual steps:${NOCOLOR}"
echo -e "${FINISHED} - Push the branch and tag upstream 'git push --atomic origin main ${GIT_TAG}'${NOCOLOR}"
echo -e "${FINISHED} - Create a new release https://github.com/google/cpu_features/releases/new${NOCOLOR}"
echo -e "${FINISHED} - Click "Choose a tag" -> ${GIT_TAG}${NOCOLOR}"
echo -e "${FINISHED} - Click "Generate the release notes"${NOCOLOR}"
echo -e "${FINISHED} - Edit description as needed${NOCOLOR}"
echo -e "${FINISHED} - Click "Publish release"${NOCOLOR}"

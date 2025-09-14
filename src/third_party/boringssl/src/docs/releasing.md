# Cutting Periodic "Releases"

The [Bazel Central Registry](https://github.com/bazelbuild/bazel-central-registry)
needs versioned snapshots and cannot consume git revisions directly. To cut a
release, do the following:

1. Pick a new version. The current scheme is `0.YYYYMMDD.0`. If we need to cut
   multiple releases in one day, increment the third digit.

2. Update `MODULE.bazel` with the new version and upload to Gerrit.

3. Once that CL lands, make a annotated git tag at the revision. This can be
   [done from Gerrit](https://boringssl-review.googlesource.com/admin/repos/boringssl,tags).
   The "Annotation" field must be non-empty. (Just using the name of the tag
   again is fine.)

4. Wait for the tag to be mirrored to GitHub, and create a corresponding
   GitHub [release](https://github.com/google/boringssl/releases/new). You
   have to be a boringssl owner on github for this to work, if it the new link
   does not work, bug davidben to invite you to the repository.

5. Download the "Source code (tar.gz)" archive from the new release and
   re-attach it to the release by clicking the edit button, and selecting "Attach New File"
   overtop of the release artifacts in github.  (The next step will check that the archive is
   correct.)

6. Clone a copy of https://github.com/bazelbuild/bazel-central-registry so that you
   can make a github pull request against it.

6. Run `go run ./util/prepare_bcr_module TAG` and follow the instructions. The
   tool does not require special privileges, though it does fetch URLs from
   GitHub and read the local checkout. It outputs a JSON file for BCR's tooling
   to consume. The instructions will tell you to run a bazelisk command.

7. CD into the root of your bazel-central-registry fork, and run the bazelisk
   command indicated by the script output above. It will add a directory to the
   repository which is untracked, use "git status " to find it, and "git add" to
   add it to what you are about to commit.  Then commit the changes and added
   directory, and submit it as a pr to bazel-central-registry.

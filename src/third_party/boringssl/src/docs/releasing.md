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
   GitHub [release](https://github.com/google/boringssl/releases/new).

5. Download the "Source code (tar.gz)" archive from the new release and
   re-attach it to the release. (The next step will check that the archive is
   correct.)

6. Run `go run ./util/prepare_bcr_module TAG` and follow the instructions. The
   tool does not require special privileges, though it does fetch URLs from
   GitHub and read the local checkout. It outputs a JSON file for BCR's tooling
   to consume.

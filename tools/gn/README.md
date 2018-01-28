# What is GN?

GN is a meta-build system that generates [Ninja](https://ninja-build.org)
build files so that you can build Chromium with Ninja.

## Why did you switch from GYP?

1. We believe GN files are more readable and more maintainable than GYP files.
2. GN is fast:
  * GN is 20x faster than GYP.
  * GN supports automatically re-running itself as needed by Ninja
    as part of the build. This eliminates the need to remember to
    re-run GN when you change a build file.
3. GN gives us better tools for enforcing dependencies (see
   `gn check` and the `visibility`, `public_deps`, and `data_deps`
   options for some examples).
4. GN gives us tools for querying the build graph; you can ask
   "what does X depend on" and "who depends on Y", for example.

## What's the status of the GYP->GN migration for Chromium?

_As of Oct 2016:_

  * All of the Chromium builds have been switched over.
  * Nearly all of the GYP files have been deleted from the Chromium repos.
  * You can no longer build with GYP as a result.
  * There are still some GYP files in place for the "Closure Compilation"
    builders that need to be converted over.
  * Some related projects (e.g., V8, Skia) may still support GYP for their
    own reasons.
  * We're still cleaning up some odds and ends like making gclient not
    still use GYP_DEFINES.

## I want more info on GN!

Read these links:

  * [Quick start](docs/quick_start.md)
  * [FAQ](docs/faq.md)
  * [Language and operation details](docs/language.md)
  * [Reference](docs/reference.md): The built-in `gn help` documentation.
  * [Style guide](docs/style_guide.md)
  * [Cross compiling and toolchains](docs/cross_compiles.md)
  * [Hacking on GN itself](docs/hacking.md)
  * [Standaline GN projects](docs/standalone.md)
  * [Pushing new binaries](docs/update_binaries.md)

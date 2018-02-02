# GN Frequently Asked Questions

[TOC]

## Where is the GN documentation?

GN has extensive built-in help, so you can run `gn help`, but you can
also see all of the help on [the reference page](reference.md). See
also the [quick start](quick_start.md) guide and the [language and
operation details](language.md).

## Can I generate XCode or Visual Studio projects?

You can generate skeleton (or wrapper) projects for Xcode, Visual Studio,
QTCreator, and Eclipse that will list the files and targets in the
build, but use Ninja to do the actual build. You cannot generate "real"
projects that look like native ones like GYP could.

Run `gn help gen` for more details.

## How do I generate common build variants?

In GN, args go with a build directory rather than being global in the
environment. To edit the args for your `out/Default` build directory:

```
gn args out/Default
```

You can set variables in that file:

  * The default is a debug build. To do a release build add
    `is_debug = false`
  * The default is a static build. To do a component build add
    `is_component_build = true`
  * The default is a developer build. To do an official build, set
    `is_official_build = true`
  * The default is Chromium branding. To do Chrome branding, set
    `is_chrome_branded = true`

## How do I do cross-compiles?

GN has robust support for doing cross compiles and building things for
multiple architectures in a single build.

See [GNCrossCompiles](cross_compiles.md) for more info.

## Can I control what targets are built by default?

Yes! If you create a group target called "default" in the top-level (root)
build file, i.e., "//:default", GN will tell Ninja to build that by
default, rather than building everything.

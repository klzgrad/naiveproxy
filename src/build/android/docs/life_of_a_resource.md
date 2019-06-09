# Life of an Android Resource

[TOC]

## Overview

This document describes how [Android Resources][android resources]
are built in Chromium's build system. It does not mention native resources
which are [processed differently][native resources].

[android resources]: https://developer.android.com/guide/topics/resources/providing-resources
[native resources]: https://www.chromium.org/developers/tools-we-use-in-chromium/grit/grit-users-guide

The steps consume the following files as inputs:
* AndroidManifest.xml
  * Including AndroidManifest.xml files from libraries, which get merged
    together
* res/ directories

The steps produce the following intermediate files:
* R.srcjar (contains R.java files)
* R.txt
* .resources.zip

The steps produce the following files within an .apk:
* AndroidManifest.xml (a binary xml file)
* resources.arsc (contains all values and configuration metadata)
* res/** (drawables and layouts)
* classes.dex (just a small portion of classes from generated R.java files)


## The Build Steps

Whenever you try to compile an apk or library target, resources go through the
following steps:

### 1. Constructs .build\_config files:

Inputs:
* GN target metadata
* Other .build_config files

Outputs:
* Target-specific .build_config file

write_build_config.py is run to record target metadata needed by future steps.
For more details, see [build_config.md](build_config.md).


### 2. Prepares resources:

Inputs:
* Target-specific build\_config file
* Target-specific Resource dirs (res/ directories)
* resources.zip files from dependencies (used to generate the R.txt/java files)

Outputs:
* Target-specific resources.zip (containing only resources in the
  target-specific resource dirs, no dependant resources here).
* Target-specific R.txt
  * Contains a list of resources and their ids (including of dependencies).
* Target-specific R.java .srcjar
  * See [What are R.java files and how are they generated](
  #how-r_java-files-are-generated)

prepare\_resources.py zips up the target-specific resource dirs and generates
R.txt and R.java .srcjars. No optimizations, crunching, etc are done on the
resources.

**The following steps apply only to apk targets (not library targets).**

### 3. Finalizes apk resources:

Inputs:
* Target-specific build\_config file
* Dependencies' resources.zip files

Output:
* Packaged resources zip (named foo.ap_) containing:
  * AndroidManifest.xml (as binary xml)
  * resources.arsc
  * res/**
* Final R.txt
  * Contains a list of resources and their ids (including of dependencies).
* Final R.java .srcjar
  * See [What are R.java files and how are they generated](
  #how-r_java-files-are-generated)


#### 3(a). Compiles resources:

For each library / resources target your apk depends on, the following happens:
* Use a regex (defined in the apk target) to remove select resources (optional).
* Convert png images to webp for binary size (optional).
* Move drawables in mdpi to non-mdpi directory ([why?](http://crbug.com/289843))
* Use `aapt2 compile` to compile xml resources to binary xml (references to
  other resources will now use the id rather than the name for faster lookup at
  runtime).
* `aapt2 compile` adds headers/metadata to 9-patch images about which parts of
  the image are stretchable vs static.
* `aapt2 compile` outputs a zip with the compiled resources (one for each
  dependency).


#### 3(b). Links resources:

After each dependency is compiled into an intermediate .zip, all those zips are
linked by the aapt2 link command which does the following:
* Use the order of dependencies supplied so that some resources clober each
  other.
* Compile the AndroidManifest.xml to binary xml (references to resources are now
  using ids rather than the string names)
* Create a resources.arsc file that has the name and values of string
  resources as well as the name and path of non-string resources (ie. layouts
  and drawables).
* Combine the compiled resources into one packaged resources apk (a zip file
  with an .ap\_ extension) that has all the resources related files.


#### 3(c). Optimizes resources:

This step obfuscates / strips resources names from the resources.arsc so that
they can be looked up only by their numeric ids (assigned in the compile
resources step). Access to resources via `Resources.getIdentifier()` no longer
work unless resources are [whitelisted](#adding-resources-to-the-whitelist).

## App Bundles and Modules:

Processing resources for bundles and modules is slightly different. Each module
has its resources compiled and linked separately (ie: it goes through the
entire process for each module). The modules are then combined to form a
bundle. Moreover, during "Finalizing the apk resources" step, bundle modules
produce a `resources.proto` file instead of a `resources.arsc` file.

Resources in a dynamic feature module may reference resources in the base
module. During the link step for feature module resources, the linked resources
of the base module are passed in. However, linking against resources currently
works only with `resources.arsc` format. Thus, when building the base module,
resources are compiled as both `resources.arsc` and `resources.proto`.

## Debugging resource related errors when resource names are obfuscated

An example message from a stacktrace could be something like this:
```
java.lang.IllegalStateException: Could not find CoordinatorLayout descendant
view with id org.chromium.chrome:id/0_resource_name_obfuscated to anchor view
android.view.ViewStub{be192d5 G.E...... ......I. 0,0-0,0 #7f0a02ad
app:id/0_resource_name_obfuscated}
```

`0_resource_name_obfuscated` is the resource name for all resources that had
their name obfuscated/stripped during the optimize resources step. To help with
debugging, the `R.txt` file is archived. The `R.txt` file contains a mapping
from resource ids to resource names and can be used to get the original resource
name from the id. In the above message the id is `0x7f0a02ad`.

For local builds, `R.txt` files are output in the `out/*/apks` directory.

For official builds, Googlers can get archived `R.txt` files next to archived
apks.

### Adding resources to the whitelist

If a resource is accessed via `getIdentifier()` it needs to be whitelisted in an
aapt2 resources config file. The config file looks like this:

```
<resource type>/<resource name>#no_obfuscate
```
eg:
```
string/app_name#no_obfuscate
id/toolbar#no_obfuscate
```

The aapt2 config file is passed to the ninja target through the
`resources_config_path` variable. To add a resource to the whitelist, check
where the config is for your target and add a new line for your resource. If
none exist, create a new config file and pass its path in your target.

### Webview resource ids

The first two bytes of a resource id is the package id. For regular apks, this
is `0x7f`. However, Webview is a shared library which gets loaded into other
apks. The package id for webview resources is assigned dynamically at runtime.
When webview is loaded it [rewrites all resources][ResourceRewriter.java] to
have the correct package id. When deobfuscating webview resource ids, disregard
the first two bytes in the id when looking it up in the `R.txt` file.

Monochrome, when loaded as webview, rewrites the package ids of resources used
by the webview portion to the correct value at runtime, otherwise, its resources
have package id `0x7f` when run as a regular apk.

[ResourceRewriter.java]: https://cs.chromium.org/chromium/src/out/android-Debug/gen/android_webview/glue/glue/generated_java/com/android/webview/chromium/ResourceRewriter.java

## How R.java files are generated

This is how a sample R.java file looks like:

```java
package org.chromium.ui;

public final class R {
    public static final class attr {
        public static final int buttonAlignment = 0x7f030038;
        public static final int buttonColor = 0x7f03003e;
        public static final int layout = 0x7f030094;
        public static final int roundedfillColor = 0x7f0300bf;
        public static final int secondaryButtonText = 0x7f0300c4;
        public static final int stackedMargin = 0x7f0300d4;
    }
    public static final class id {
        public static final int apart = 0x7f080021;
        public static final int dropdown_body_footer_divider = 0x7f08003d;
        public static final int dropdown_body_list = 0x7f08003e;
        public static final int dropdown_footer = 0x7f08003f;
    }
    public static final class layout {
        public static final int dropdown_item = 0x7f0a0022;
        public static final int dropdown_window = 0x7f0a0023;
    }
}
```

R.java is a list of static classes, each with multiple static fields containing
ids. These ids are used in java code to reference resources in the apk. The
R.java file generated via the prepare resources step above has temporary ids
which are not marked `final`. That R.java file is only used so that javac can
compile the java code that references R.*.

The R.java generated during the finalize apk resources step has
permanent ids. These ids are marked as `final` (except webview resources that
need to be [rewritten at runtime](#webview-resource-ids)).

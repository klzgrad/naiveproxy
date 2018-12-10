# Tools for Analyzing Chrome's Binary Size

These tools currently focus on supporting Android. They somewhat work with
Linux builds. As for Windows, some great tools already exist and are documented
here:

 * https://www.chromium.org/developers/windows-binary-sizes

There is also a dedicated mailing-list for binary size discussions:

 * https://groups.google.com/a/chromium.org/forum/#!forum/binary-size

Bugs are tracked here:

 * [Tools > BinarySize](https://bugs.chromium.org/p/chromium/issues/list?q=component%3ATools>BinarySize)

[TOC]

## diagnose_bloat.py

Determine the cause of binary size bloat between two commits. Works for Android
and Linux (although Linux symbol diffs have issues, as noted below).

### How it Works

1. Builds multiple revisions using release GN args.
   * Default is to build just two revisions (before & after commit)
   * Rather than building, can fetch build artifacts and `.size` files from perf
     bots (`--cloud`)
1. Measures all outputs using `resource_size.py` and `supersize`.
1. Saves & displays a breakdown of the difference in binary sizes.

### Example Usage

``` bash
# Build and diff monochrome_public_apk HEAD^ and HEAD.
tools/binary_size/diagnose_bloat.py HEAD -v

# Build and diff monochrome_apk HEAD^ and HEAD.
tools/binary_size/diagnose_bloat.py HEAD --enable-chrome-android-internal -v

# Build and diff monochrome_public_apk HEAD^ and HEAD without is_official_build.
tools/binary_size/diagnose_bloat.py HEAD --gn-args="is_official_build=false" -v

# Diff BEFORE_REV and AFTER_REV using build artifacts downloaded from perf bots.
tools/binary_size/diagnose_bloat.py AFTER_REV --reference-rev BEFORE_REV --cloud -v

# Fetch a .size, libmonochrome.so, and MonochromePublic.apk from perf bots (Googlers only):
tools/binary_size/diagnose_bloat.py AFTER_REV --cloud --unstripped --single

# Build and diff all contiguous revs in range BEFORE_REV..AFTER_REV for src/v8.
tools/binary_size/diagnose_bloat.py AFTER_REV --reference-rev BEFORE_REV --subrepo v8 --all -v

# Display detailed usage info (there are many options).
tools/binary_size/diagnose_bloat.py -h
```

## Super Size

Collect, archive, and analyze Chrome's binary size.
Supports Android and Linux (although Linux
[has issues](https://bugs.chromium.org/p/chromium/issues/detail?id=717550)).

`.size` files are archived on perf builders so that regressions can be quickly
analyzed (via `diagnose_bloat.py --cloud`).

`.size` files are archived on official builders so that symbols can be diff'ed
between milestones.

### Technical Details

#### What's in a .size File?

`.size` files are gzipped plain text files that contain:

1. A list of section sizes, including:
   * .so sections as reported by `readelf -S`
   * .pak and .dex sections for apk files
1. Metadata (apk size, GN args, filenames, timestamps, git revision, build id),
1. A list of symbols, including name, address, size,
  padding (caused by alignment), and associated source/object files.

#### How are Symbols Collected?

##### Native Symbols

1. Symbol list is Extracted from linker `.map` file.
   * Map files contain some unique pieces of information compared to `nm`
      output, such as `** merge strings` entries, and some unnamed symbols
      (which although unnamed, contain the `.o` path).
1. `.o` files are mapped to `.cc` files by parsing `.ninja` files.
   * This means that `.h` files are never listed as sources. No information
     about inlined symbols is gathered.
1. `** merge strings` symbols are further broken down into individual string
   literal symbols. This is done by reading string literals from `.o` files, and
   then searching for them within the `** merge strings` sections.
1. Symbol aliases:
   * Aliases have the same address and size, but report their `.pss` as
      `.size / .num_aliases`.
   * Type 1: Different names. Caused by identical code folding.
     * These are collected from debug information via `nm elf-file`.
   * Type 2: Same names, different paths. Caused by inline functions defined in
     `.h` files.
     * These are collected by running `nm` on each `.o` file.
     * Normally represented using one alias per path, but are sometimes
       collapsed into a single symbol with a path of `{shared}/$SYMBOL_COUNT`.
       This collapsing is done only for symbols owned by a large number of paths.
   * Type 3: String literals that are de-duped at link-time.
     * These are found as part of the string literal extraction process.

##### Pak Symbols

1. Grit creates a mapping between numeric id and textual id for grd files.
   * A side effect of pak whitelist generation is a mapping of `.cc` to numeric
     id.
   * A complete per-apk mapping of numeric id to textual id is stored in the
     `output_dir/size-info` dir.
1. `supersize` uses these two mappings to find associated source files for the
  pak entries found in all of the apk's `.pak` files.
   * Pak entries with the same name are merged into a single symbol.
     * This is the case of pak files for translations.
   * The original grd file paths are stored in the full name of each symbol.

##### Dex Symbols

1. Java compile targets create a mapping between java fully qualified names
  (FQN) and source files.
   * For `.java` files the FQN of the public class is mapped to the file.
   * For `.srcjar` files the FQN of the public class is mapped to the `.srcjar`
     file path.
   * A complete per-apk class FQN to source mapping is stored in the
     `output_dir/size-info` dir.
1. The `apkanalyzer` sdk tool is used to find the size and FQN of entries in
  the dex file.
   * If a proguard `.mapping` file is available, that is used to get back the
     original FQN.
1. The output from `apkanalyzer` is used by `supersize` along with the mapping
  file to find associated source files for the dex entries found in all of the
  apk's `.dex` files.

##### Common Symbols

1. Shared bytes are stored in symbols with names starting with `Overhead: `.
   * Elf file, dex file, pak files, apk files all have compression overhead.
   * These are treated as padding-only symbols to de-emphasize them in diffs.
   * It is expected that these symbols have minor fluctuations since they are
     affected by changes in compressibility.
1. All other files in an apk have one symbol each under the `.other` section
  with their corresponding path in the apk as their associated path.

#### What Other Processing Happens?

1. Path normalization:
   * Prefixes are removed: `out/Release/`, `gen/`, `obj/`
   * Archive names made more pathy: `foo/bar.a(baz.o)` -> `foo/bar.a/baz.o`
   * Shared symbols do not store the complete source paths. Instead, the
     common ancestor is computed and stored as the path.
      * Example: `base/{shared}/3` (the "3" means three different files contain
        the symbol)

1. Name normalization:
   * `(anonymous::)` is removed from names (and stored as a symbol flag).
   * `[clone]` suffix removed (and stored as a symbol flag).
   * `vtable for FOO` -> `Foo [vtable]`
   * Mangling done by linkers is undone (e.g. prefixing with "unlikely.")
   * Names are processed into:
     * `name`: Name without template and argument parameters
     * `template_name`: Name without argument parameters.
     * `full_name`: Name with all parameters.

1. Clustering:
   * Compiler & linker optimizations can cause symbols to be broken into
     multiple parts to become candidates for inlining ("partial inlining").
   * These symbols are sometimes suffixed with "`[clone]`" (removed by
     normalization).
   * Clustering creates groups containing all pieces of a symbol (in the case
     where multiple pieces remain after inlining).
   * Clustering is done by default on `SizeInfo.symbols`. To view unclustered
     symbols, use `SizeInfo.raw_symbols`.

1. Diffing:
   * Some heuristics for matching up before/after symbols.

1. Simulated compression:
   * Only some `.pak` files are compressed and others are kept uncompressed.
   * To get a reasonable idea of actual impact to final apk size, we use a
     constant compression factor for all the compressed `.pak` files.
     * This prevents swings in compressed sizes for all symbols when new
       entries are added or old entries are removed.
     * The constant is chosen so that it minimizes overall discrepancy with
       actual total compressed sizes.

#### Is Super Size a Generic Tool?

No. Most of the logic is would could work for any ELF executable. However, being
a generic tool is not a goal. Some examples of existing Chrome-specific logic:

 * Assumes `.ninja` build rules are available.
 * Heuristic for locating `.so` given `.apk`.
 * Requires `size-info` dir in output directory to analyze `.pak` and `.dex`
   files.

### Usage: archive

Collect size information and dump it into a `.size` file.

*** note
**Note:** Refer to
[diagnose_bloat.py](https://cs.chromium.org/search/?q=file:diagnose_bloat.py+gn_args)
for list of GN args to build a Release binary (or just use the tool with --single).

**Googlers:** If you just want a `.size` for a commit on master:

    GIT_REV="HEAD~200"
    tools/binary_size/diagnose_bloat.py --single --cloud --unstripped $GIT_REV
***

Example Usage:

``` bash
# Android:
ninja -C out/Release -j 1000 apks/ChromePublic.apk
tools/binary_size/supersize archive chrome.size --apk-file out/Release/apks/ChromePublic.apk -v

# Linux:
ninja -C out/Release -j 1000 chrome
tools/binary_size/supersize archive chrome.size --elf-file out/Release/chrome -v
```

### Usage: html_report

Creates an interactive size breakdown (by source path) as a stand-alone html
report.

Example output: https://notwoods.github.io/chrome-supersize-reports/

Example Usage:

``` bash
# Creates the data file ./report.ndjson, generated based on ./chrome.size
tools/binary_size/supersize html_report chrome.size --report-file report.ndjson -v

# Includes every symbol in the data file, although it will take longer to load.
tools/binary_size/supersize html_report chrome.size --report-file report.ndjson --all-symbols

# Create a data file showing a diff between two .size files.
tools/binary_size/supersize html_report after.size --diff-with before.size --report-file report.ndjson
```

### Usage: start_server

Locally view the data file generated by `html_report`, by starting a web server
that links to a data file.

Example Usage:

``` bash
# Starts a local server to view the data in ./report.ndjson
tools/binary_size/supersize start_server report.ndjson

# Set a custom address and port.
tools/binary_size/supersize start_server report.ndjson -a localhost -p 8080
```

### Usage: diff

A convenience command equivalent to: `console before.size after.size --query='Print(Diff(size_info1, size_info2))'`

Example Usage:

``` bash
tools/binary_size/supersize diff before.size after.size --all
```

### Usage: console

Starts a Python interpreter where you can run custom queries, or run pre-made
queries from `canned_queries.py`.

Example Usage:

```bash
# Prints size infomation and exits (does not enter interactive mode).
tools/binary_size/supersize console chrome.size --query='Print(size_info)'

# Enters a Python REPL (it will print more guidance).
tools/binary_size/supersize console chrome.size
```

Example session:

``` python
>>> ShowExamples()  # Get some inspiration.
...
>>> sorted = size_info.symbols.WhereInSection('t').Sorted()
>>> Print(sorted)  # Have a look at the largest symbols.
...
>>> sym = sorted.WhereNameMatches('TrellisQuantizeBlock')[0]
>>> Disassemble(sym)  # Time to learn assembly.
...
>>> help(canned_queries)
...
>>> Print(canned_queries.TemplatesByName(depth=-1))
...
>>> syms = size_info.symbols.WherePathMatches(r'skia').Sorted()
>>> Print(syms, verbose=True)  # Show full symbol names with parameter types.
...
>>> # Dump all string literals from skia files to "strings.txt".
>>> Print((t[1] for t in ReadStringLiterals(syms)), to_file='strings.txt')
```

### Roadmap

1. [Better Linux support](https://bugs.chromium.org/p/chromium/issues/detail?id=717550) (clang+lld+lto vs gcc+gold).
1. More `console` features:
   * Add `SplitByName()` - Like `GroupByName()`, but recursive.
   * A canned query, that does what ShowGlobals does (as described in [Windows Binary Sizes](https://www.chromium.org/developers/windows-binary-sizes)).
1. Integrate with `resource_sizes.py` so that it tracks size of major
   components separately: chrome vs blink vs skia vs v8.
1. Add dependency graph info, perhaps just on a per-file basis.
   * No idea how to do this, but Windows can do it via `tools\win\linker_verbose_tracking.py`

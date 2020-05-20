This directory is used to store GN arg mapping for Chrome OS boards.

The board listed in your .gclient file's `"cros_board"="some_board"` custom_vars
variable will have a corresponding .gni file here, populated by a gclient hook.
To use these files in a build, simply add the following line to your GN args:
```
import("//build/args/chromeos/${some_board}.gni")
```

That will produce a Chrome OS build of Chrome very similar to what is shipped
for that device. You can also supply additional args or even overwrite ones
supplied in the .gni file after the `import()` line. For example, the following
args will produce a debug build of Chrome for board=eve using goma:
```
import("//build/args/chromeos/eve.gni")

is_debug = true
use_goma = true
goma_dir = "/path/to/goma/"
```

TODO(bpastene): Add list support to gclient and allow multiple boards to be
specified in the .gclient file.

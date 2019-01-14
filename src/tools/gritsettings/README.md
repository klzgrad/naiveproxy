### tools/gritsettings README

This directory contains several files that apply global to the Chrome resource
generation system (which uses GRIT - see tools/grit).

**resource_ids**: This file is used to assign starting resource ids for
resources and strings used by Chromium. This is done to ensure that resource ids
are unique across all the grd files. If you are adding a new grd file, please
add a new entry to this file.

**translation_expectations.pyl**: Specifies which grd files should be translated
and into which languages they should be translated. Used by the internal
translation process.

**startup_resources_[platform].txt**: These files provide a pre-determined
resource id ordering that will be used by GRIT when assigning resources ids. The
goal is to have the resource loaded during Chrome startup be ordered first in
the .pak files, so that fewer page faults are suffered during Chrome start up.
To update or generate one of these files, follow these instructions:

  1. Build a Chrome official release build and launch it with command line:
     `--print-resource-ids` and save the output to a file (e.g. res.txt).

  2. Generate the startup_resources_[platform].txt via the following command
     (you can redirect its output to the new file location):

     `
     tools/grit/grit/format/gen_predetermined_ids.py res_ids.txt out/gn
     `

     In the above command, res_ids.txt is the file produced in step 1 and out/gn
     is you Chrome build directory where you compiled Chrome. The output of the
     command can be added as a new startup_resource_[platform]

  3. If this is a new file, modify `tools/grit/grit_rule.gni` to set its path
     via `grit_predetermined_resource_ids_file` for the given platform.

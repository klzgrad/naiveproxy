### Updating APKs in this folder (for new milestones, builders, or APKs)

1. Find the commit as close as possible to the current branch point (i.e. if the
latest builds are m59, we want to compare to the commit before the m58 branch
point).

2. Download and unzip build artifacts from the relevant perf builder.

    gsutil.py cp 'gs://chrome-perf/Android Builder/full-build-linux_COMMITHASH.zip' /dest/dir

3. Unzip. Steps 4, 5, and 6 must be done for MonochromePublic.apk for
`gs://chrome-perf/Android Builder` and ChromeModernPublic.apk for
`gs://chrome-perf/Android arm64 Builder` (and can be done for additional APKS,
but these are the ones used by `build/android/resource_sizes.py`)

4. Upload the apk (replacing the bolded parts again - note that we use
**Android_Builder** instead of **Android Builder** (replace spaces with
underscores):

    upload_to_google_storage.py --bucket 'chromium-android-tools/apks/Android_Builder/58' dest/dir/MonochromePublic.apk

5. Move the generated .sha1 file to the corresponding place in
`//build/android/binary_size/apks/`. In this case, the path would be
`//build/android/binary_size/apks/Android_Builder/58`

6. Commit the added .sha1 files and (optionally) update the `CURRENT_MILESTONE`
in apk_downloader.py

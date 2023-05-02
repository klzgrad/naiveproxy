"""Tools for building QUICHE tests."""

load("@bazel_skylib//lib:dicts.bzl", "dicts")
load("@bazel_skylib//lib:paths.bzl", "paths")

def test_suite_from_source_list(name, srcs, **kwargs):
    """
    Generates a test target for every individual test source file specified.

    Args:
        name: the name of the resulting test_suite target.
        srcs: the list of source files from which the test targets are generated.
        **kwargs: other arguments that are passed to the cc_test rule directly.s
    """

    tests = []
    for sourcefile in srcs:
        if not sourcefile.endswith("_test.cc"):
            fail("All source files passed to test_suite_from_source_list() must end with _test.cc")
        test_name, _ = paths.split_extension(paths.basename(sourcefile))
        extra_kwargs = {}
        if test_name == "end_to_end_test":
            extra_kwargs["shard_count"] = 16
        native.cc_test(
            name = test_name,
            srcs = [sourcefile],
            **dicts.add(kwargs, extra_kwargs)
        )
        tests.append(test_name)
    native.test_suite(name = name, tests = tests)

# Copyright 2024 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
""" The module to provide measurements when it's supported. """

import os
import sys

from contextlib import AbstractContextManager

from common import DIR_SRC_ROOT

PROTO_DIR = os.path.abspath(
    os.path.join(DIR_SRC_ROOT, 'build', 'util', 'lib', 'proto'))
if os.path.isdir(PROTO_DIR):
    sys.path.append(PROTO_DIR)
    # pylint: disable=import-error, unused-import
    from measures import average, clear, count, data_points, dump, tag, \
            time_consumption
else:

    class Dummy(AbstractContextManager):
        """Dummy implementation when measures components do not exist."""

        # pylint: disable=no-self-use
        def record(self, *_) -> None:
            """Dummy implementation of Measure.record."""

        # pylint: disable=no-self-use
        def dump(self) -> None:
            """Dummy implementation of Measure.dump."""
            # Shouldn't be called.
            assert False

        # pylint: disable=no-self-use
        def __enter__(self) -> None:
            pass

        # pylint: disable=no-self-use
        def __exit__(self, *_) -> bool:
            return False

    def average(*_) -> Dummy:
        """Dummy implementation of measures.average."""
        return Dummy()

    def count(*_) -> Dummy:
        """Dummy implementation of measures.count."""
        return Dummy()

    def clear(*_) -> None:
        """Dummy implementation of measures.clear."""

    def data_points(*_) -> Dummy:
        """Dummy implementation of measures.data_points."""
        return Dummy()

    def time_consumption(*_) -> Dummy:
        """Dummy implementation of measures.time_consumption."""
        return Dummy()

    def dump(*_) -> None:
        """Dummy implementation of measures.dump."""

    def tag(*_) -> None:
        """Dummy implementation of measures.tag."""

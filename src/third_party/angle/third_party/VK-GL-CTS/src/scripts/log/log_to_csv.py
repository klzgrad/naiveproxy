# -*- coding: utf-8 -*-

#-------------------------------------------------------------------------
# drawElements Quality Program utilities
# --------------------------------------
#
# Copyright 2015 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
#-------------------------------------------------------------------------

import sys
from log_parser import BatchResultParser

def batchResultToCsv (filename):
	parser = BatchResultParser()
	results = parser.parseFile(filename)

	for result in results:
		print("%s,%s" % (result.name, result.statusCode))

if __name__ == "__main__":
	if len(sys.argv) != 2:
		print("%s: [qpa log]" % sys.argv[0])
		sys.exit(-1)

	batchResultToCsv(sys.argv[1])

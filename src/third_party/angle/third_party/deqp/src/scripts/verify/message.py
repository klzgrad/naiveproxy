# -*- coding: utf-8 -*-

#-------------------------------------------------------------------------
# Vulkan CTS
# ----------
#
# Copyright (c) 2016 Google Inc.
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

import os

class ValidationMessage:
	TYPE_ERROR		= 0
	TYPE_WARNING	= 1

	def __init__ (self, type, filename, message):
		self.type		= type
		self.filename	= filename
		self.message	= message

	def __str__ (self):
		prefix = {self.TYPE_ERROR: "ERROR: ", self.TYPE_WARNING: "WARNING: "}
		return prefix[self.type] + os.path.basename(self.filename) + ": " + self.message

def error (filename, message):
	return ValidationMessage(ValidationMessage.TYPE_ERROR, filename, message)

def warning (filename, message):
	return ValidationMessage(ValidationMessage.TYPE_WARNING, filename, message)

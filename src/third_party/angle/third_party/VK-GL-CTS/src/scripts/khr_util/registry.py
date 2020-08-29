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

import sys, logging, re
from lxml import etree
from collections import OrderedDict
from functools import wraps, partial

log = logging.getLogger(__name__)

debug = log.debug
info = log.info
warning = log.warning

def warnElem(elem, fmt, *args):
	warning('%s:%d, %s %s: ' + fmt, elem.base, elem.sourceline, elem.tag, elem.get('name') or '', *args)

class Object(object):
	def __init__(self, **kwargs):
		self.__dict__.update(kwargs)

class Located(Object):
	location = None

class Group(Located): pass
class Enum(Located): pass
class Enums(Located):
	name = None
	comment = None
	enums = None

class Type(Located):
	location = None
	name=None
	definition=None
	api=None
	requires=None

def makeObject(cls, elem, **kwargs):
	kwargs.setdefault('name', elem.get('name'))
	kwargs.setdefault('comment', elem.get('comment'))
	kwargs['location'] = (elem.base, elem.sourceline)
	return cls(**kwargs)

def parseEnum(eEnum):
	return makeObject(
		Enum, eEnum,
		value=eEnum.get('value'),
		type=eEnum.get('type'),
		alias=eEnum.get('alias'))

class Param(Located): pass

class Command(Located):
	name=None
	declaration=None
	type=None
	ptype=None
	group=None
	params=None
	alias=None

class Interface(Object): pass

class Index:
	def __init__(self, items=[], **kwargs):
		self.index = {}
		self.items = []
		self.__dict__.update(kwargs)
		self.update(items)

	def append(self, item):
		keys = self.getkeys(item)
		for key in keys:
			self[key] = item
		self.items.append(item)

	def update(self, items):
		for item in items:
			self.append(item)

	def __iter__(self):
		return iter(self.items)

	def nextkey(self, key):
		raise KeyError

	def getkeys(self, item):
		return []

	def __contains__(self, key):
		return key in self.index

	def __setitem__(self, key, item):
		if key in self.index:
			self.duplicateKey(key, item)
		else:
			self.index[key] = item

	def duplicateKey(self, key, item):
		warning("Duplicate %s: %r", type(item).__name__.lower(), key)

	def __getitem__(self, key):
		try:
			while True:
				try:
					return self.index[key]
				except KeyError:
					pass
				key = self.nextkey(key)
		except KeyError:
			item = self.missingKey(key)
			self.append(item)
			return item

	def missingKey(self, key):
		raise KeyError(key)

	def __len__(self):
		return len(self.items)

class ElemNameIndex(Index):
	def getkeys(self, item):
		return [item.get('name')]

	def duplicateKey(self, key, item):
		warnElem(item, "Duplicate key: %s", key)

class CommandIndex(Index):
	def getkeys(self, item):
		#BOZA: No reason to add alias: it has its own entry in enums in xml file
		#return [(name, api)] + ([(alias, api)] if alias is not None else [])
		return [item.findtext('proto/name')]

class NameApiIndex(Index):
	def getkeys(self, item):
		return [(item.get('name'), item.get('api'))]

	def nextkey(self, key):
		if len(key) == 2 and key[1] is not None:
			return key[0], None
		raise KeyError

	def duplicateKey(self, key, item):
		warnElem(item, "Duplicate key: %s", key)

class TypeIndex(NameApiIndex):
	def getkeys(self, item):
		return [(item.get('name') or item.findtext('name'), item.get('api'))]

class EnumIndex(NameApiIndex):
	def getkeys(self, item):
		name, api, alias = (item.get(attrib) for attrib in ['name', 'api', 'alias'])
		#BOZA: No reason to add alias: it has its own entry in enums
		#return [(name, api)] + ([(alias, api)] if alias is not None else [])
		return [(name, api)]

	def duplicateKey(self, nameapipair, item):
		(name, api) = nameapipair
		if name == item.get('alias'):
			warnElem(item, "Alias already present: %s", name)
		else:
			warnElem(item, "Already present")

class Registry:
	def __init__(self, eRegistry):
		self.types = TypeIndex(eRegistry.findall('types/type'))
		self.groups = ElemNameIndex(eRegistry.findall('groups/group'))
		self.enums = EnumIndex(eRegistry.findall('enums/enum'))
		for eEnum in self.enums:
			groupName = eEnum.get('group')
			if groupName is not None:
				self.groups[groupName] = eEnum
		self.commands = CommandIndex(eRegistry.findall('commands/command'))
		self.features = ElemNameIndex(eRegistry.findall('feature'))
		self.apis = {}
		for eFeature in self.features:
			self.apis.setdefault(eFeature.get('api'), []).append(eFeature)
		for apiFeatures in self.apis.values():
			apiFeatures.sort(key=lambda eFeature: eFeature.get('number'))
		self.extensions = ElemNameIndex(eRegistry.findall('extensions/extension'))
		self.element = eRegistry

	def getFeatures(self, api, checkVersion=None):
		return [eFeature for eFeature in self.apis[api]
				if checkVersion is None or checkVersion(eFeature.get('number'))]

class NameIndex(Index):
	createMissing = None
	kind = "item"

	def getkeys(self, item):
		return [item.name]

	def missingKey(self, key):
		if self.createMissing:
			warning("Reference to implicit %s: %r", self.kind, key)
			return self.createMissing(name=key)
		else:
			raise KeyError

def matchApi(api1, api2):
	return api1 is None or api2 is None or api1 == api2

class Interface(Object):
	pass

def extractAlias(eCommand):
	aliases = eCommand.xpath('alias/@name')
	return aliases[0] if aliases else None

def getExtensionName(eExtension):
	return eExtension.get('name')

def extensionSupports(eExtension, api, profile=None):
	if api == 'gl' and profile == 'core':
		needSupport = 'glcore'
	else:
		needSupport = api
	supporteds = eExtension.get('supported').split('|')
	return needSupport in supporteds

class InterfaceSpec(Object):
	def __init__(self):
		self.enums = set()
		self.types = set()
		self.commands = set()
		self.versions = set()

	def addComponent(self, eComponent):
		if eComponent.tag == 'require':
			def modify(items, item): items.add(item)
		else:
			assert eComponent.tag == 'remove'
			def modify(items, item):
				try:
					items.remove(item)
				except KeyError:
					warning("Tried to remove absent item: %s", item)
		for typeName in eComponent.xpath('type/@name'):
			modify(self.types, typeName)
		for enumName in eComponent.xpath('enum/@name'):
			modify(self.enums, enumName)
		for commandName in eComponent.xpath('command/@name'):
			modify(self.commands, commandName)

	def addComponents(self, elem, api, profile=None):
		for eComponent in elem.xpath('require|remove'):
			cApi = eComponent.get('api')
			cProfile = eComponent.get('profile')
			if (matchApi(api, eComponent.get('api')) and
				matchApi(profile, eComponent.get('profile'))):
				self.addComponent(eComponent)

	def addFeature(self, eFeature, api=None, profile=None, force=False):
		info('Feature %s', eFeature.get('name'))
		if not matchApi(api, eFeature.get('api')):
			if not force: return
			warnElem(eFeature, 'API %s is not supported', api)
		self.addComponents(eFeature, api, profile)
		self.versions.add(eFeature.get('name'))

	def addExtension(self, eExtension, api=None, profile=None, force=False):
		if not extensionSupports(eExtension, api, profile):
			if not force: return
			warnElem(eExtension, '%s is not supported in API %s' % (getExtensionName(eExtension), api))
		self.addComponents(eExtension, api, profile)

def createInterface(registry, spec, api=None):
	def parseType(eType):
		# todo: apientry
		#requires = eType.get('requires')
		#if requires is not None:
		#    types[requires]
		return makeObject(
			Type, eType,
			name=eType.get('name') or eType.findtext('name'),
			definition=''.join(eType.xpath('.//text()')),
			api=eType.get('api'),
			requires=eType.get('requires'))

	def createType(name):
		info('Add type %s', name)
		try:
			return parseType(registry.types[name, api])
		except KeyError:
			return Type(name=name)

	def createEnum(enumName):
		info('Add enum %s', enumName)
		return parseEnum(registry.enums[enumName, api])

	def extractPtype(elem):
		ePtype = elem.find('ptype')
		if ePtype is None:
			return None
		return types[ePtype.text]

	def extractGroup(elem):
		groupName = elem.get('group')
		if groupName is None:
			return None
		return groups[groupName]

	def parseParam(eParam):
		return makeObject(
			Param, eParam,
			name=eParam.get('name') or eParam.findtext('name'),
			declaration=''.join(eParam.xpath('.//text()')).strip(),
			type=''.join(eParam.xpath('(.|ptype)/text()')).strip(),
			ptype=extractPtype(eParam),
			group=extractGroup(eParam))

	def createCommand(commandName):
		info('Add command %s', commandName)
		eCmd = registry.commands[commandName]
		eProto = eCmd.find('proto')
		return makeObject(
			Command, eCmd,
			name=eCmd.findtext('proto/name'),
			declaration=''.join(eProto.xpath('.//text()')).strip(),
			type=''.join(eProto.xpath('(.|ptype)/text()')).strip(),
			ptype=extractPtype(eProto),
			group=extractGroup(eProto),
			alias=extractAlias(eCmd),
			params=NameIndex(list(map(parseParam, eCmd.findall('param')))))

	def createGroup(name):
		info('Add group %s', name)
		try:
			eGroup = registry.groups[name]
		except KeyError:
			return Group(name=name)
		return makeObject(
			Group, eGroup,
			# Missing enums are often from exotic extensions. Don't create dummy entries,
			# just filter them out.
			enums=NameIndex(enums[name] for name in eGroup.xpath('enum/@name')
							if name in enums))

	def sortedIndex(items):
		# Some groups have no location set, due to it is absent in gl.xml file
		# for example glGetFenceivNV uses group FenceNV which is not declared
		#	<command>
		#		<proto>void <name>glGetFenceivNV</name></proto>
		#		<param group="FenceNV"><ptype>GLuint</ptype> <name>fence</name></param>
		# Python 2 ignores it. Avoid sorting to allow Python 3 to continue

		enableSort=True
		for item in items:
			if item.location is None:
				enableSort=False
				warning("Location not found for %s: %s", type(item).__name__.lower(), item.name)

		if enableSort:
			sortedItems = sorted(items, key=lambda item: item.location)
		else:
			sortedItems = items
		return NameIndex(sortedItems)

	groups = NameIndex(createMissing=createGroup, kind="group")
	types = NameIndex(list(map(createType, spec.types)),
					  createMissing=createType, kind="type")
	enums = NameIndex(list(map(createEnum, spec.enums)),
					  createMissing=Enum, kind="enum")
	commands = NameIndex(list(map(createCommand, spec.commands)),
						createMissing=Command, kind="command")
	versions = sorted(spec.versions)

	# This is a mess because the registry contains alias chains whose
	# midpoints might not be included in the interface even though
	# endpoints are.
	for command in commands:
		alias = command.alias
		aliasCommand = None
		while alias is not None:
			aliasCommand = registry.commands[alias]
			alias = extractAlias(aliasCommand)
		command.alias = None
		if aliasCommand is not None:
			name = aliasCommand.findtext('proto/name')
			if name in commands:
				command.alias = commands[name]

	sortedTypes=sortedIndex(types)
	sortedEnums=sortedIndex(enums)
	sortedGroups=sortedIndex(groups)
	sortedCommands=sortedIndex(commands)

	ifc=Interface(
		types=sortedTypes,
		enums=sortedEnums,
		groups=sortedGroups,
		commands=sortedCommands,
		versions=versions)

	return ifc


def spec(registry, api, version=None, profile=None, extensionNames=[], protects=[], force=False):
	available = set(protects)
	spec = InterfaceSpec()

	if version is None or version is False:
		def check(v): return False
	elif version is True:
		def check(v): return True
	else:
		def check(v): return v <= version

#	BOZA TODO: I suppose adding primitive types will remove a lot of warnings
#	spec.addComponents(registry.types, api, profile)

	for eFeature in registry.getFeatures(api, check):
		spec.addFeature(eFeature, api, profile, force)

	for extName in extensionNames:
		eExtension = registry.extensions[extName]
		protect = eExtension.get('protect')
		if protect is not None and protect not in available:
			warnElem(eExtension, "Unavailable dependency %s", protect)
			if not force:
				continue
		spec.addExtension(eExtension, api, profile, force)
		available.add(extName)

	return spec

def interface(registry, api, **kwargs):
	s = spec(registry, api, **kwargs)
	return createInterface(registry, s, api)

def parse(path):
	return Registry(etree.parse(path))

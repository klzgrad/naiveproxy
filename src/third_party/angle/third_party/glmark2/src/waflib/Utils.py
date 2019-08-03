#! /usr/bin/env python
# encoding: utf-8
# WARNING! Do not edit! https://waf.io/book/index.html#_obtaining_the_waf_file

import os,sys,errno,traceback,inspect,re,datetime,platform,base64
try:
	import cPickle
except ImportError:
	import pickle as cPickle
if os.name=='posix'and sys.version_info[0]<3:
	try:
		import subprocess32 as subprocess
	except ImportError:
		import subprocess
else:
	import subprocess
from collections import deque,defaultdict
try:
	import _winreg as winreg
except ImportError:
	try:
		import winreg
	except ImportError:
		winreg=None
from waflib import Errors
try:
	from hashlib import md5
except ImportError:
	try:
		from md5 import md5
	except ImportError:
		pass
try:
	import threading
except ImportError:
	if not'JOBS'in os.environ:
		os.environ['JOBS']='1'
	class threading(object):
		pass
	class Lock(object):
		def acquire(self):
			pass
		def release(self):
			pass
	threading.Lock=threading.Thread=Lock
SIG_NIL='SIG_NIL_SIG_NIL_'.encode()
O644=420
O755=493
rot_chr=['\\','|','/','-']
rot_idx=0
class ordered_iter_dict(dict):
	def __init__(self,*k,**kw):
		self.lst=deque()
		dict.__init__(self,*k,**kw)
	def clear(self):
		dict.clear(self)
		self.lst=deque()
	def __setitem__(self,key,value):
		if key in dict.keys(self):
			self.lst.remove(key)
		dict.__setitem__(self,key,value)
		self.lst.append(key)
	def __delitem__(self,key):
		dict.__delitem__(self,key)
		try:
			self.lst.remove(key)
		except ValueError:
			pass
	def __iter__(self):
		return reversed(self.lst)
	def keys(self):
		return reversed(self.lst)
class lru_node(object):
	__slots__=('next','prev','key','val')
	def __init__(self):
		self.next=self
		self.prev=self
		self.key=None
		self.val=None
class lru_cache(object):
	__slots__=('maxlen','table','head')
	def __init__(self,maxlen=100):
		self.maxlen=maxlen
		self.table={}
		self.head=lru_node()
		for x in range(maxlen-1):
			node=lru_node()
			node.prev=self.head.prev
			node.next=self.head
			node.prev.next=node
			node.next.prev=node
	def __getitem__(self,key):
		node=self.table[key]
		if node is self.head:
			return node.val
		node.prev.next=node.next
		node.next.prev=node.prev
		node.next=self.head.next
		node.prev=self.head
		node.next.prev=node
		node.prev.next=node
		self.head=node
		return node.val
	def __setitem__(self,key,val):
		node=self.head=self.head.next
		try:
			del self.table[node.key]
		except KeyError:
			pass
		node.key=key
		node.val=val
		self.table[key]=node
is_win32=os.sep=='\\'or sys.platform=='win32'
def readf(fname,m='r',encoding='ISO8859-1'):
	if sys.hexversion>0x3000000 and not'b'in m:
		m+='b'
		f=open(fname,m)
		try:
			txt=f.read()
		finally:
			f.close()
		if encoding:
			txt=txt.decode(encoding)
		else:
			txt=txt.decode()
	else:
		f=open(fname,m)
		try:
			txt=f.read()
		finally:
			f.close()
	return txt
def writef(fname,data,m='w',encoding='ISO8859-1'):
	if sys.hexversion>0x3000000 and not'b'in m:
		data=data.encode(encoding)
		m+='b'
	f=open(fname,m)
	try:
		f.write(data)
	finally:
		f.close()
def h_file(fname):
	f=open(fname,'rb')
	m=md5()
	try:
		while fname:
			fname=f.read(200000)
			m.update(fname)
	finally:
		f.close()
	return m.digest()
def readf_win32(f,m='r',encoding='ISO8859-1'):
	flags=os.O_NOINHERIT|os.O_RDONLY
	if'b'in m:
		flags|=os.O_BINARY
	if'+'in m:
		flags|=os.O_RDWR
	try:
		fd=os.open(f,flags)
	except OSError:
		raise IOError('Cannot read from %r'%f)
	if sys.hexversion>0x3000000 and not'b'in m:
		m+='b'
		f=os.fdopen(fd,m)
		try:
			txt=f.read()
		finally:
			f.close()
		if encoding:
			txt=txt.decode(encoding)
		else:
			txt=txt.decode()
	else:
		f=os.fdopen(fd,m)
		try:
			txt=f.read()
		finally:
			f.close()
	return txt
def writef_win32(f,data,m='w',encoding='ISO8859-1'):
	if sys.hexversion>0x3000000 and not'b'in m:
		data=data.encode(encoding)
		m+='b'
	flags=os.O_CREAT|os.O_TRUNC|os.O_WRONLY|os.O_NOINHERIT
	if'b'in m:
		flags|=os.O_BINARY
	if'+'in m:
		flags|=os.O_RDWR
	try:
		fd=os.open(f,flags)
	except OSError:
		raise OSError('Cannot write to %r'%f)
	f=os.fdopen(fd,m)
	try:
		f.write(data)
	finally:
		f.close()
def h_file_win32(fname):
	try:
		fd=os.open(fname,os.O_BINARY|os.O_RDONLY|os.O_NOINHERIT)
	except OSError:
		raise OSError('Cannot read from %r'%fname)
	f=os.fdopen(fd,'rb')
	m=md5()
	try:
		while fname:
			fname=f.read(200000)
			m.update(fname)
	finally:
		f.close()
	return m.digest()
readf_unix=readf
writef_unix=writef
h_file_unix=h_file
if hasattr(os,'O_NOINHERIT')and sys.hexversion<0x3040000:
	readf=readf_win32
	writef=writef_win32
	h_file=h_file_win32
try:
	x=''.encode('hex')
except LookupError:
	import binascii
	def to_hex(s):
		ret=binascii.hexlify(s)
		if not isinstance(ret,str):
			ret=ret.decode('utf-8')
		return ret
else:
	def to_hex(s):
		return s.encode('hex')
to_hex.__doc__="""
Return the hexadecimal representation of a string

:param s: string to convert
:type s: string
"""
def listdir_win32(s):
	if not s:
		try:
			import ctypes
		except ImportError:
			return[x+':\\'for x in'ABCDEFGHIJKLMNOPQRSTUVWXYZ']
		else:
			dlen=4
			maxdrives=26
			buf=ctypes.create_string_buffer(maxdrives*dlen)
			ndrives=ctypes.windll.kernel32.GetLogicalDriveStringsA(maxdrives*dlen,ctypes.byref(buf))
			return[str(buf.raw[4*i:4*i+2].decode('ascii'))for i in range(int(ndrives/dlen))]
	if len(s)==2 and s[1]==":":
		s+=os.sep
	if not os.path.isdir(s):
		e=OSError('%s is not a directory'%s)
		e.errno=errno.ENOENT
		raise e
	return os.listdir(s)
listdir=os.listdir
if is_win32:
	listdir=listdir_win32
def num2ver(ver):
	if isinstance(ver,str):
		ver=tuple(ver.split('.'))
	if isinstance(ver,tuple):
		ret=0
		for i in range(4):
			if i<len(ver):
				ret+=256**(3-i)*int(ver[i])
		return ret
	return ver
def ex_stack():
	return traceback.format_exc()
def to_list(val):
	if isinstance(val,str):
		return val.split()
	else:
		return val
def split_path_unix(path):
	return path.split('/')
def split_path_cygwin(path):
	if path.startswith('//'):
		ret=path.split('/')[2:]
		ret[0]='/'+ret[0]
		return ret
	return path.split('/')
re_sp=re.compile('[/\\\\]+')
def split_path_win32(path):
	if path.startswith('\\\\'):
		ret=re_sp.split(path)[2:]
		ret[0]='\\'+ret[0]
		return ret
	return re_sp.split(path)
msysroot=None
def split_path_msys(path):
	if path.startswith(('/','\\'))and not path.startswith(('\\','\\\\')):
		global msysroot
		if not msysroot:
			msysroot=subprocess.check_output(['cygpath','-w','/']).decode(sys.stdout.encoding or'iso8859-1')
			msysroot=msysroot.strip()
		path=os.path.normpath(msysroot+os.sep+path)
	return split_path_win32(path)
if sys.platform=='cygwin':
	split_path=split_path_cygwin
elif is_win32:
	if os.environ.get('MSYSTEM'):
		split_path=split_path_msys
	else:
		split_path=split_path_win32
else:
	split_path=split_path_unix
split_path.__doc__="""
Splits a path by / or \\; do not confuse this function with with ``os.path.split``

:type  path: string
:param path: path to split
:return:     list of string
"""
def check_dir(path):
	if not os.path.isdir(path):
		try:
			os.makedirs(path)
		except OSError as e:
			if not os.path.isdir(path):
				raise Errors.WafError('Cannot create the folder %r'%path,ex=e)
def check_exe(name,env=None):
	if not name:
		raise ValueError('Cannot execute an empty string!')
	def is_exe(fpath):
		return os.path.isfile(fpath)and os.access(fpath,os.X_OK)
	fpath,fname=os.path.split(name)
	if fpath and is_exe(name):
		return os.path.abspath(name)
	else:
		env=env or os.environ
		for path in env['PATH'].split(os.pathsep):
			path=path.strip('"')
			exe_file=os.path.join(path,name)
			if is_exe(exe_file):
				return os.path.abspath(exe_file)
	return None
def def_attrs(cls,**kw):
	for k,v in kw.items():
		if not hasattr(cls,k):
			setattr(cls,k,v)
def quote_define_name(s):
	fu=re.sub('[^a-zA-Z0-9]','_',s)
	fu=re.sub('_+','_',fu)
	fu=fu.upper()
	return fu
def h_list(lst):
	return md5(repr(lst).encode()).digest()
def h_fun(fun):
	try:
		return fun.code
	except AttributeError:
		try:
			h=inspect.getsource(fun)
		except EnvironmentError:
			h='nocode'
		try:
			fun.code=h
		except AttributeError:
			pass
		return h
def h_cmd(ins):
	if isinstance(ins,str):
		ret=ins
	elif isinstance(ins,list)or isinstance(ins,tuple):
		ret=str([h_cmd(x)for x in ins])
	else:
		ret=str(h_fun(ins))
	if sys.hexversion>0x3000000:
		ret=ret.encode('iso8859-1','xmlcharrefreplace')
	return ret
reg_subst=re.compile(r"(\\\\)|(\$\$)|\$\{([^}]+)\}")
def subst_vars(expr,params):
	def repl_var(m):
		if m.group(1):
			return'\\'
		if m.group(2):
			return'$'
		try:
			return params.get_flat(m.group(3))
		except AttributeError:
			return params[m.group(3)]
	return reg_subst.sub(repl_var,expr)
def destos_to_binfmt(key):
	if key=='darwin':
		return'mac-o'
	elif key in('win32','cygwin','uwin','msys'):
		return'pe'
	return'elf'
def unversioned_sys_platform():
	s=sys.platform
	if s.startswith('java'):
		from java.lang import System
		s=System.getProperty('os.name')
		if s=='Mac OS X':
			return'darwin'
		elif s.startswith('Windows '):
			return'win32'
		elif s=='OS/2':
			return'os2'
		elif s=='HP-UX':
			return'hp-ux'
		elif s in('SunOS','Solaris'):
			return'sunos'
		else:s=s.lower()
	if s=='powerpc':
		return'darwin'
	if s=='win32'or s=='os2':
		return s
	if s=='cli'and os.name=='nt':
		return'win32'
	return re.split('\d+$',s)[0]
def nada(*k,**kw):
	pass
class Timer(object):
	def __init__(self):
		self.start_time=datetime.datetime.utcnow()
	def __str__(self):
		delta=datetime.datetime.utcnow()-self.start_time
		days=delta.days
		hours,rem=divmod(delta.seconds,3600)
		minutes,seconds=divmod(rem,60)
		seconds+=delta.microseconds*1e-6
		result=''
		if days:
			result+='%dd'%days
		if days or hours:
			result+='%dh'%hours
		if days or hours or minutes:
			result+='%dm'%minutes
		return'%s%.3fs'%(result,seconds)
def read_la_file(path):
	sp=re.compile(r'^([^=]+)=\'(.*)\'$')
	dc={}
	for line in readf(path).splitlines():
		try:
			_,left,right,_=sp.split(line.strip())
			dc[left]=right
		except ValueError:
			pass
	return dc
def run_once(fun):
	cache={}
	def wrap(*k):
		try:
			return cache[k]
		except KeyError:
			ret=fun(*k)
			cache[k]=ret
			return ret
	wrap.__cache__=cache
	wrap.__name__=fun.__name__
	return wrap
def get_registry_app_path(key,filename):
	if not winreg:
		return None
	try:
		result=winreg.QueryValue(key,"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\%s.exe"%filename[0])
	except WindowsError:
		pass
	else:
		if os.path.isfile(result):
			return result
def lib64():
	if os.sep=='/':
		if platform.architecture()[0]=='64bit':
			if os.path.exists('/usr/lib64')and not os.path.exists('/usr/lib32'):
				return'64'
	return''
def sane_path(p):
	return os.path.abspath(os.path.expanduser(p))
process_pool=[]
def get_process():
	try:
		return process_pool.pop()
	except IndexError:
		filepath=os.path.dirname(os.path.abspath(__file__))+os.sep+'processor.py'
		cmd=[sys.executable,'-c',readf(filepath)]
		return subprocess.Popen(cmd,stdout=subprocess.PIPE,stdin=subprocess.PIPE,bufsize=0)
def run_prefork_process(cmd,kwargs,cargs):
	if not'env'in kwargs:
		kwargs['env']=dict(os.environ)
	try:
		obj=base64.b64encode(cPickle.dumps([cmd,kwargs,cargs]))
	except TypeError:
		return run_regular_process(cmd,kwargs,cargs)
	proc=get_process()
	if not proc:
		return run_regular_process(cmd,kwargs,cargs)
	proc.stdin.write(obj)
	proc.stdin.write('\n'.encode())
	proc.stdin.flush()
	obj=proc.stdout.readline()
	if not obj:
		raise OSError('Preforked sub-process %r died'%proc.pid)
	process_pool.append(proc)
	ret,out,err,ex,trace=cPickle.loads(base64.b64decode(obj))
	if ex:
		if ex=='OSError':
			raise OSError(trace)
		elif ex=='ValueError':
			raise ValueError(trace)
		else:
			raise Exception(trace)
	return ret,out,err
def run_regular_process(cmd,kwargs,cargs={}):
	proc=subprocess.Popen(cmd,**kwargs)
	if kwargs.get('stdout')or kwargs.get('stderr'):
		out,err=proc.communicate(**cargs)
		status=proc.returncode
	else:
		out,err=(None,None)
		status=proc.wait(**cargs)
	return status,out,err
def run_process(cmd,kwargs,cargs={}):
	if kwargs.get('stdout')and kwargs.get('stderr'):
		return run_prefork_process(cmd,kwargs,cargs)
	else:
		return run_regular_process(cmd,kwargs,cargs)
def alloc_process_pool(n,force=False):
	global run_process,get_process,alloc_process_pool
	if not force:
		n=max(n-len(process_pool),0)
	try:
		lst=[get_process()for x in range(n)]
	except OSError:
		run_process=run_regular_process
		get_process=alloc_process_pool=nada
	else:
		for x in lst:
			process_pool.append(x)
if sys.platform=='cli'or not sys.executable:
	run_process=run_regular_process
	get_process=alloc_process_pool=nada

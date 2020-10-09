#! /usr/bin/env python
# encoding: utf-8
# WARNING! Do not edit! https://waf.io/book/index.html#_obtaining_the_waf_file

import os
from waflib.TaskGen import feature,after_method,taskgen_method
from waflib import Utils,Task,Logs,Options
from waflib.Tools import ccroot
testlock=Utils.threading.Lock()
@feature('test')
@after_method('apply_link','process_use')
def make_test(self):
	if not getattr(self,'link_task',None):
		return
	tsk=self.create_task('utest',self.link_task.outputs)
	if getattr(self,'ut_str',None):
		self.ut_run,lst=Task.compile_fun(self.ut_str,shell=getattr(self,'ut_shell',False))
		tsk.vars=lst+tsk.vars
	if getattr(self,'ut_cwd',None):
		if isinstance(self.ut_cwd,str):
			if os.path.isabs(self.ut_cwd):
				self.ut_cwd=self.bld.root.make_node(self.ut_cwd)
			else:
				self.ut_cwd=self.path.make_node(self.ut_cwd)
	else:
		self.ut_cwd=tsk.inputs[0].parent
	if not hasattr(self,'ut_paths'):
		paths=[]
		for x in self.tmp_use_sorted:
			try:
				y=self.bld.get_tgen_by_name(x).link_task
			except AttributeError:
				pass
			else:
				if not isinstance(y,ccroot.stlink_task):
					paths.append(y.outputs[0].parent.abspath())
		self.ut_paths=os.pathsep.join(paths)+os.pathsep
	if not hasattr(self,'ut_env'):
		self.ut_env=dct=dict(os.environ)
		def add_path(var):
			dct[var]=self.ut_paths+dct.get(var,'')
		if Utils.is_win32:
			add_path('PATH')
		elif Utils.unversioned_sys_platform()=='darwin':
			add_path('DYLD_LIBRARY_PATH')
			add_path('LD_LIBRARY_PATH')
		else:
			add_path('LD_LIBRARY_PATH')
@taskgen_method
def add_test_results(self,tup):
	Logs.debug("ut: %r",tup)
	self.utest_result=tup
	try:
		self.bld.utest_results.append(tup)
	except AttributeError:
		self.bld.utest_results=[tup]
class utest(Task.Task):
	color='PINK'
	after=['vnum','inst']
	vars=[]
	def runnable_status(self):
		if getattr(Options.options,'no_tests',False):
			return Task.SKIP_ME
		ret=super(utest,self).runnable_status()
		if ret==Task.SKIP_ME:
			if getattr(Options.options,'all_tests',False):
				return Task.RUN_ME
		return ret
	def get_test_env(self):
		return self.generator.ut_env
	def post_run(self):
		super(utest,self).post_run()
		if getattr(Options.options,'clear_failed_tests',False)and self.waf_unit_test_results[1]:
			self.generator.bld.task_sigs[self.uid()]=None
	def run(self):
		if hasattr(self.generator,'ut_run'):
			return self.generator.ut_run(self)
		self.ut_exec=getattr(self.generator,'ut_exec',[self.inputs[0].abspath()])
		if getattr(self.generator,'ut_fun',None):
			self.generator.ut_fun(self)
		testcmd=getattr(self.generator,'ut_cmd',False)or getattr(Options.options,'testcmd',False)
		if testcmd:
			self.ut_exec=(testcmd%' '.join(self.ut_exec)).split(' ')
		return self.exec_command(self.ut_exec)
	def exec_command(self,cmd,**kw):
		Logs.debug('runner: %r',cmd)
		proc=Utils.subprocess.Popen(cmd,cwd=self.get_cwd().abspath(),env=self.get_test_env(),stderr=Utils.subprocess.PIPE,stdout=Utils.subprocess.PIPE)
		(stdout,stderr)=proc.communicate()
		self.waf_unit_test_results=tup=(self.inputs[0].abspath(),proc.returncode,stdout,stderr)
		testlock.acquire()
		try:
			return self.generator.add_test_results(tup)
		finally:
			testlock.release()
	def get_cwd(self):
		return self.generator.ut_cwd
def summary(bld):
	lst=getattr(bld,'utest_results',[])
	if lst:
		Logs.pprint('CYAN','execution summary')
		total=len(lst)
		tfail=len([x for x in lst if x[1]])
		Logs.pprint('CYAN','  tests that pass %d/%d'%(total-tfail,total))
		for(f,code,out,err)in lst:
			if not code:
				Logs.pprint('CYAN','    %s'%f)
		Logs.pprint('CYAN','  tests that fail %d/%d'%(tfail,total))
		for(f,code,out,err)in lst:
			if code:
				Logs.pprint('CYAN','    %s'%f)
def set_exit_code(bld):
	lst=getattr(bld,'utest_results',[])
	for(f,code,out,err)in lst:
		if code:
			msg=[]
			if out:
				msg.append('stdout:%s%s'%(os.linesep,out.decode('utf-8')))
			if err:
				msg.append('stderr:%s%s'%(os.linesep,err.decode('utf-8')))
			bld.fatal(os.linesep.join(msg))
def options(opt):
	opt.add_option('--notests',action='store_true',default=False,help='Exec no unit tests',dest='no_tests')
	opt.add_option('--alltests',action='store_true',default=False,help='Exec all unit tests',dest='all_tests')
	opt.add_option('--clear-failed',action='store_true',default=False,help='Force failed unit tests to run again next time',dest='clear_failed_tests')
	opt.add_option('--testcmd',action='store',default=False,help='Run the unit tests using the test-cmd string'' example "--test-cmd="valgrind --error-exitcode=1'' %s" to run under valgrind',dest='testcmd')

#! /usr/bin/env python
# encoding: utf-8
# WARNING! Do not edit! https://waf.io/book/index.html#_obtaining_the_waf_file

import random
try:
	from queue import Queue
except ImportError:
	from Queue import Queue
from waflib import Utils,Task,Errors,Logs
GAP=20
class Consumer(Utils.threading.Thread):
	__slots__=('task','spawner')
	def __init__(self,spawner,task):
		Utils.threading.Thread.__init__(self)
		self.task=task
		self.spawner=spawner
		self.setDaemon(1)
		self.start()
	def run(self):
		try:
			if not self.spawner.master.stop:
				self.task.process()
		finally:
			self.spawner.sem.release()
			self.spawner.master.out.put(self.task)
			self.task=None
			self.spawner=None
class Spawner(Utils.threading.Thread):
	def __init__(self,master):
		Utils.threading.Thread.__init__(self)
		self.master=master
		self.sem=Utils.threading.Semaphore(master.numjobs)
		self.setDaemon(1)
		self.start()
	def run(self):
		try:
			self.loop()
		except Exception:
			pass
	def loop(self):
		master=self.master
		while 1:
			task=master.ready.get()
			self.sem.acquire()
			task.log_display(task.generator.bld)
			Consumer(self,task)
class Parallel(object):
	def __init__(self,bld,j=2):
		self.numjobs=j
		self.bld=bld
		self.outstanding=Utils.deque()
		self.frozen=Utils.deque()
		self.ready=Queue(0)
		self.out=Queue(0)
		self.count=0
		self.processed=1
		self.stop=False
		self.error=[]
		self.biter=None
		self.dirty=False
		self.spawner=Spawner(self)
	def get_next_task(self):
		if not self.outstanding:
			return None
		return self.outstanding.popleft()
	def postpone(self,tsk):
		if random.randint(0,1):
			self.frozen.appendleft(tsk)
		else:
			self.frozen.append(tsk)
	def refill_task_list(self):
		while self.count>self.numjobs*GAP:
			self.get_out()
		while not self.outstanding:
			if self.count:
				self.get_out()
			elif self.frozen:
				try:
					cond=self.deadlock==self.processed
				except AttributeError:
					pass
				else:
					if cond:
						msg='check the build order for the tasks'
						for tsk in self.frozen:
							if not tsk.run_after:
								msg='check the methods runnable_status'
								break
						lst=[]
						for tsk in self.frozen:
							lst.append('%s\t-> %r'%(repr(tsk),[id(x)for x in tsk.run_after]))
						raise Errors.WafError('Deadlock detected: %s%s'%(msg,''.join(lst)))
				self.deadlock=self.processed
			if self.frozen:
				self.outstanding.extend(self.frozen)
				self.frozen.clear()
			elif not self.count:
				self.outstanding.extend(next(self.biter))
				self.total=self.bld.total()
				break
	def add_more_tasks(self,tsk):
		if getattr(tsk,'more_tasks',None):
			self.outstanding.extend(tsk.more_tasks)
			self.total+=len(tsk.more_tasks)
	def get_out(self):
		tsk=self.out.get()
		if not self.stop:
			self.add_more_tasks(tsk)
		self.count-=1
		self.dirty=True
		return tsk
	def add_task(self,tsk):
		self.ready.put(tsk)
	def skip(self,tsk):
		tsk.hasrun=Task.SKIPPED
	def error_handler(self,tsk):
		if hasattr(tsk,'scan')and hasattr(tsk,'uid'):
			try:
				del self.bld.imp_sigs[tsk.uid()]
			except KeyError:
				pass
		if not self.bld.keep:
			self.stop=True
		self.error.append(tsk)
	def task_status(self,tsk):
		try:
			return tsk.runnable_status()
		except Exception:
			self.processed+=1
			tsk.err_msg=Utils.ex_stack()
			if not self.stop and self.bld.keep:
				self.skip(tsk)
				if self.bld.keep==1:
					if Logs.verbose>1 or not self.error:
						self.error.append(tsk)
					self.stop=True
				else:
					if Logs.verbose>1:
						self.error.append(tsk)
				return Task.EXCEPTION
			tsk.hasrun=Task.EXCEPTION
			self.error_handler(tsk)
			return Task.EXCEPTION
	def start(self):
		self.total=self.bld.total()
		while not self.stop:
			self.refill_task_list()
			tsk=self.get_next_task()
			if not tsk:
				if self.count:
					continue
				else:
					break
			if tsk.hasrun:
				self.processed+=1
				continue
			if self.stop:
				break
			st=self.task_status(tsk)
			if st==Task.RUN_ME:
				self.count+=1
				self.processed+=1
				if self.numjobs==1:
					tsk.log_display(tsk.generator.bld)
					try:
						tsk.process()
					finally:
						self.out.put(tsk)
				else:
					self.add_task(tsk)
			if st==Task.ASK_LATER:
				self.postpone(tsk)
			elif st==Task.SKIP_ME:
				self.processed+=1
				self.skip(tsk)
				self.add_more_tasks(tsk)
		while self.error and self.count:
			self.get_out()
		self.ready.put(None)
		assert(self.count==0 or self.stop)

// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_MESSAGE_LOOP_MESSAGE_LOOP_H_
#define BASE_MESSAGE_LOOP_MESSAGE_LOOP_H_

#include <memory>
#include <queue>
#include <string>

#include "base/base_export.h"
#include "base/callback_forward.h"
#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "base/memory/scoped_refptr.h"
#include "base/message_loop/message_loop_current.h"
#include "base/message_loop/message_pump.h"
#include "base/message_loop/pending_task_queue.h"
#include "base/message_loop/timer_slack.h"
#include "base/pending_task.h"
#include "base/run_loop.h"
#include "base/synchronization/lock.h"
#include "base/threading/sequence_local_storage_map.h"
#include "base/threading/thread_checker.h"
#include "base/time/time.h"
#include "build/build_config.h"

namespace base {
class MessageLoopImpl;

namespace sequence_manager {
class TaskQueue;
class LazyThreadControllerForTest;
namespace internal {
class SequenceManagerImpl;
class ThreadControllerImpl;
}
}  // namespace sequence_manager

// A MessageLoop is used to process events for a particular thread.  There is
// at most one MessageLoop instance per thread.
//
// Events include at a minimum Task instances submitted to the MessageLoop's
// TaskRunner. Depending on the Type of message pump used by the MessageLoop
// other events such as UI messages may be processed.  On Windows APC calls (as
// time permits) and signals sent to a registered set of HANDLEs may also be
// processed.
//
// The MessageLoop's API should only be used directly by its owner (and users
// which the owner opts to share a MessageLoop* with). Other ways to access
// subsets of the MessageLoop API:
//   - base::RunLoop : Drive the MessageLoop from the thread it's bound to.
//   - base::Thread/SequencedTaskRunnerHandle : Post back to the MessageLoop
//     from a task running on it.
//   - SequenceLocalStorageSlot : Bind external state to this MessageLoop.
//   - base::MessageLoopCurrent : Access statically exposed APIs of this
//     MessageLoop.
//   - Embedders may provide their own static accessors to post tasks on
//     specific loops (e.g. content::BrowserThreads).
//
// NOTE: Unless otherwise specified, a MessageLoop's methods may only be called
// on the thread where the MessageLoop's Run method executes.
//
// NOTE: MessageLoop has task reentrancy protection.  This means that if a
// task is being processed, a second task cannot start until the first task is
// finished.  Reentrancy can happen when processing a task, and an inner
// message pump is created.  That inner pump then processes native messages
// which could implicitly start an inner task.  Inner message pumps are created
// with dialogs (DialogBox), common dialogs (GetOpenFileName), OLE functions
// (DoDragDrop), printer functions (StartDoc) and *many* others.
//
// Sample workaround when inner task processing is needed:
//   HRESULT hr;
//   {
//     MessageLoopCurrent::ScopedNestableTaskAllower allow;
//     hr = DoDragDrop(...); // Implicitly runs a modal message loop.
//   }
//   // Process |hr| (the result returned by DoDragDrop()).
//
// Please be SURE your task is reentrant (nestable) and all global variables
// are stable and accessible before calling SetNestableTasksAllowed(true).

class BASE_EXPORT MessageLoopBase {
 public:
  MessageLoopBase() = default;
  virtual ~MessageLoopBase() = default;

  // A MessageLoop has a particular type, which indicates the set of
  // asynchronous events it may process in addition to tasks and timers.
  //
  // TYPE_DEFAULT
  //   This type of ML only supports tasks and timers.
  //
  // TYPE_UI
  //   This type of ML also supports native UI events (e.g., Windows messages).
  //   See also MessageLoopForUI.
  //
  // TYPE_IO
  //   This type of ML also supports asynchronous IO.  See also
  //   MessageLoopForIO.
  //
  // TYPE_JAVA
  //   This type of ML is backed by a Java message handler which is responsible
  //   for running the tasks added to the ML. This is only for use on Android.
  //   TYPE_JAVA behaves in essence like TYPE_UI, except during construction
  //   where it does not use the main thread specific pump factory.
  //
  // TYPE_CUSTOM
  //   MessagePump was supplied to constructor.
  //
  enum Type {
    TYPE_DEFAULT,
    TYPE_UI,
    TYPE_CUSTOM,
    TYPE_IO,
#if defined(OS_ANDROID)
    TYPE_JAVA,
#endif  // defined(OS_ANDROID)
  };

  // Returns true if this loop is |type|. This allows subclasses (especially
  // those in tests) to specialize how they are identified.
  virtual bool IsType(Type type) const = 0;

  // Returns the name of the thread this message loop is bound to. This function
  // is only valid when this message loop is running, BindToCurrentThread has
  // already been called and has an "happens-before" relationship with this call
  // (this relationship is obtained implicitly by the MessageLoop's task posting
  // system unless calling this very early).
  virtual std::string GetThreadName() const = 0;

  using DestructionObserver = MessageLoopCurrent::DestructionObserver;

  // Add a DestructionObserver, which will start receiving notifications
  // immediately.
  virtual void AddDestructionObserver(
      DestructionObserver* destruction_observer) = 0;

  // Remove a DestructionObserver.  It is safe to call this method while a
  // DestructionObserver is receiving a notification callback.
  virtual void RemoveDestructionObserver(
      DestructionObserver* destruction_observer) = 0;

  // TODO(altimin,yutak): Replace with base::TaskObserver.
  using TaskObserver = MessageLoopCurrent::TaskObserver;

  // These functions can only be called on the same thread that |this| is
  // running on.
  // These functions must not be called from a TaskObserver callback.
  virtual void AddTaskObserver(TaskObserver* task_observer) = 0;
  virtual void RemoveTaskObserver(TaskObserver* task_observer) = 0;

  // When this functionality is enabled, the queue time will be recorded for
  // posted tasks.
  virtual void SetAddQueueTimeToTasks(bool enable) = 0;

  // Returns true if this is the active MessageLoop for the current thread.
  virtual bool IsBoundToCurrentThread() const = 0;

  // Returns true if the message loop is idle (ignoring delayed tasks). This is
  // the same condition which triggers DoWork() to return false: i.e.
  // out of tasks which can be processed at the current run-level -- there might
  // be deferred non-nestable tasks remaining if currently in a nested run
  // level.
  virtual bool IsIdleForTesting() = 0;

  // Returns the MessagePump owned by this MessageLoop if any.
  virtual MessagePump* GetMessagePump() const = 0;

  // Sets a new TaskRunner for this message loop. If the message loop was
  // already bound, this must be called on the thread to which it is bound.
  // TODO(alexclarke): Remove this as part of https://crbug.com/825327.
  virtual void SetTaskRunner(
      scoped_refptr<SingleThreadTaskRunner> task_runner) = 0;

  // Gets the TaskRunner associated with this message loop.
  // TODO(alexclarke): Remove this as part of https://crbug.com/825327.
  virtual scoped_refptr<SingleThreadTaskRunner> GetTaskRunner() = 0;

  // Binds the MessageLoop to the current thread using |pump|.
  virtual void BindToCurrentThread(std::unique_ptr<MessagePump> pump) = 0;

  // Returns true if the MessageLoop retains any tasks inside it.
  virtual bool HasTasks() = 0;

  // Deletes all tasks associated with this MessageLoop. Note that the tasks
  // can post other tasks when destructed.
  virtual void DeletePendingTasks() = 0;

 protected:
  friend class MessageLoop;
  friend class MessageLoopForUI;
  friend class MessageLoopCurrent;
  friend class MessageLoopCurrentForIO;
  friend class MessageLoopCurrentForUI;
  friend class sequence_manager::internal::ThreadControllerImpl;

  // Explicitly allow or disallow task execution. Task execution is disallowed
  // implicitly when we enter a nested runloop.
  virtual void SetTaskExecutionAllowed(bool allowed) = 0;

  // Whether task execution is allowed at the moment.
  virtual bool IsTaskExecutionAllowed() const = 0;

#if defined(OS_IOS) || defined(OS_ANDROID)
  virtual void AttachToMessagePump() = 0;
#endif

  // Set the timer slack for this message loop.
  // TODO(alexclarke): Remove this as part of https://crbug.com/891670.
  virtual void SetTimerSlack(TimerSlack timer_slack) = 0;
};

class BASE_EXPORT MessageLoop {
 public:
  // For migration convenience we define the Type enum.
  using Type = MessageLoopBase::Type;
  static constexpr Type TYPE_DEFAULT = Type::TYPE_DEFAULT;
  static constexpr Type TYPE_UI = Type::TYPE_UI;
  static constexpr Type TYPE_CUSTOM = Type::TYPE_CUSTOM;
  static constexpr Type TYPE_IO = Type::TYPE_IO;
#if defined(OS_ANDROID)
  static constexpr Type TYPE_JAVA = Type::TYPE_JAVA;
#endif  // defined(OS_ANDROID)

  // Normally, it is not necessary to instantiate a MessageLoop.  Instead, it
  // is typical to make use of the current thread's MessageLoop instance.
  explicit MessageLoop(Type type = TYPE_DEFAULT);
  // Creates a TYPE_CUSTOM MessageLoop with the supplied MessagePump, which must
  // be non-NULL.
  explicit MessageLoop(std::unique_ptr<MessagePump> pump);

  virtual ~MessageLoop();

  using MessagePumpFactory = std::unique_ptr<MessagePump>();
  // Uses the given base::MessagePumpForUIFactory to override the default
  // MessagePump implementation for 'TYPE_UI'. Returns true if the factory
  // was successfully registered.
  static bool InitMessagePumpForUIFactory(MessagePumpFactory* factory);

  // Creates the default MessagePump based on |type|. Caller owns return
  // value.
  static std::unique_ptr<MessagePump> CreateMessagePumpForType(Type type);

  // Set the timer slack for this message loop.
  void SetTimerSlack(TimerSlack timer_slack);

  // Returns true if this loop is |type|. This allows subclasses (especially
  // those in tests) to specialize how they are identified.
  virtual bool IsType(Type type) const;

  // Returns the type passed to the constructor.
  Type type() const { return type_; }

  // Returns the name of the thread this message loop is bound to. This function
  // is only valid when this message loop is running, BindToCurrentThread has
  // already been called and has an "happens-before" relationship with this call
  // (this relationship is obtained implicitly by the MessageLoop's task posting
  // system unless calling this very early).
  std::string GetThreadName() const;

  // Sets a new TaskRunner for this message loop. If the message loop was
  // already bound, this must be called on the thread to which it is bound.
  void SetTaskRunner(scoped_refptr<SingleThreadTaskRunner> task_runner);

  // Gets the TaskRunner associated with this message loop.
  scoped_refptr<SingleThreadTaskRunner> task_runner() const;

  // TODO(yutak): Replace all the use sites with base::TaskObserver.
  using TaskObserver = MessageLoopCurrent::TaskObserver;

  // These functions can only be called on the same thread that |this| is
  // running on.
  // These functions must not be called from a TaskObserver callback.
  void AddTaskObserver(TaskObserver* task_observer);
  void RemoveTaskObserver(TaskObserver* task_observer);

  // Returns true if this is the active MessageLoop for the current thread.
  bool IsBoundToCurrentThread() const;

  // Returns true if the message loop is idle (ignoring delayed tasks). This is
  // the same condition which triggers DoWork() to return false: i.e.
  // out of tasks which can be processed at the current run-level -- there might
  // be deferred non-nestable tasks remaining if currently in a nested run
  // level.
  // TODO(alexclarke): Make this const when MessageLoopImpl goes away.
  bool IsIdleForTesting();

  MessageLoopBase* GetMessageLoopBase();

  enum class BackendType {
    MESSAGE_LOOP_IMPL,
    SEQUENCE_MANAGER,
  };

  //----------------------------------------------------------------------------
 protected:
  using MessagePumpFactoryCallback =
      OnceCallback<std::unique_ptr<MessagePump>()>;

  // Common protected constructor. Other constructors delegate the
  // initialization to this constructor.
  // A subclass can invoke this constructor to create a message_loop of a
  // specific type with a custom loop. The implementation does not call
  // BindToCurrentThread. If this constructor is invoked directly by a subclass,
  // then the subclass must subsequently bind the message loop.
  MessageLoop(Type type, MessagePumpFactoryCallback pump_factory);

  // Configure various members and bind this message loop to the current thread.
  void BindToCurrentThread();

  // A raw pointer to the MessagePump handed-off to |backend_|.
  // Valid for the lifetime of |backend_|.
  MessagePump* pump_;

  // The actual implentation of the MessageLoop — either MessageLoopImpl or
  // SequenceManager-based.
  const std::unique_ptr<MessageLoopBase> backend_;
  // SequenceManager-based backend requires an explicit initialisation of the
  // default task queue.
  scoped_refptr<sequence_manager::TaskQueue> default_task_queue_;

 private:
  friend class MessageLoopTaskRunnerTest;
  friend class MessageLoopTypedTest;
  friend class ScheduleWorkTest;
  friend class Thread;
  friend class sequence_manager::LazyThreadControllerForTest;
  friend class sequence_manager::internal::SequenceManagerImpl;
  FRIEND_TEST_ALL_PREFIXES(MessageLoopTest, DeleteUnboundLoop);

  friend class MessageLoopTaskRunnerTest;
  FRIEND_TEST_ALL_PREFIXES(MessageLoopTest, DeleteUnboundLoop);

  // Contstructor which allows to specify the backend explicitly.
  MessageLoop(Type type,
              MessagePumpFactoryCallback pump_factory,
              BackendType backend_type);

  // Creates a MessageLoop without binding to a thread.
  // If |type| is TYPE_CUSTOM non-null |pump_factory| must be also given
  // to create a message pump for this message loop.  Otherwise a default
  // message pump for the |type| is created.
  //
  // It is valid to call this to create a new message loop on one thread,
  // and then pass it to the thread where the message loop actually runs.
  // The message loop's BindToCurrentThread() method must be called on the
  // thread the message loop runs on, before calling Run().
  // Before BindToCurrentThread() is called, only Post*Task() functions can
  // be called on the message loop.
  static std::unique_ptr<MessageLoop> CreateUnbound(
      Type type,
      MessagePumpFactoryCallback pump_factory);

  // Initializers for |backend_| and related fields.
  std::unique_ptr<MessageLoopBase> CreateSequenceManager(Type type);
  std::unique_ptr<MessageLoopBase> CreateMessageLoopImpl(Type type);

  scoped_refptr<sequence_manager::TaskQueue> CreateDefaultTaskQueue(
      BackendType backend_type);

  // Returns |next_run_time| capped at 1 day from |recent_time_|. This is used
  // to mitigate https://crbug.com/850450 where some platforms are unhappy with
  // delays > 100,000,000 seconds. In practice, a diagnosis metric showed that
  // no sleep > 1 hour ever completes (always interrupted by an earlier
  // MessageLoop event) and 99% of completed sleeps are the ones scheduled for
  // <= 1 second. Details @ https://crrev.com/c/1142589.
  TimeTicks CapAtOneDay(TimeTicks next_run_time);

  std::unique_ptr<MessagePump> CreateMessagePump();

  const Type type_;

  // pump_factory_.Run() is called to create a message pump for this loop
  // if |type_| is TYPE_CUSTOM and |pump_| is null.
  MessagePumpFactoryCallback pump_factory_;

  // Id of the thread this message loop is bound to. Initialized once when the
  // MessageLoop is bound to its thread and constant forever after.
  PlatformThreadId thread_id_ = kInvalidThreadId;

  // Verifies that calls are made on the thread on which BindToCurrentThread()
  // was invoked.
  THREAD_CHECKER(bound_thread_checker_);

  DISALLOW_COPY_AND_ASSIGN(MessageLoop);
};

#if !defined(OS_NACL)

//-----------------------------------------------------------------------------
// MessageLoopForUI extends MessageLoop with methods that are particular to a
// MessageLoop instantiated with TYPE_UI.
//
// By instantiating a MessageLoopForUI on the current thread, the owner enables
// native UI message pumping.
//
// MessageLoopCurrentForUI is exposed statically on its thread via
// MessageLoopCurrentForUI::Get() to provide additional functionality.
//
class BASE_EXPORT MessageLoopForUI : public MessageLoop {
 public:
  explicit MessageLoopForUI(Type type = TYPE_UI);

#if defined(OS_IOS)
  // On iOS, the main message loop cannot be Run().  Instead call Attach(),
  // which connects this MessageLoop to the UI thread's CFRunLoop and allows
  // PostTask() to work.
  void Attach();
#endif

#if defined(OS_ANDROID)
  // On Android there are cases where we want to abort immediately without
  // calling Quit(), in these cases we call Abort().
  void Abort();

  // True if this message pump has been aborted.
  bool IsAborted();

  // Since Run() is never called on Android, and the message loop is run by the
  // java Looper, quitting the RunLoop won't join the thread, so we need a
  // callback to run when the RunLoop goes idle to let the Java thread know when
  // it can safely quit.
  void QuitWhenIdle(base::OnceClosure callback);
#endif

#if defined(OS_WIN)
  // See method of the same name in the Windows MessagePumpForUI implementation.
  void EnableWmQuit();
#endif
};

// Do not add any member variables to MessageLoopForUI!  This is important b/c
// MessageLoopForUI is often allocated via MessageLoop(TYPE_UI).  Any extra
// data that you need should be stored on the MessageLoop's pump_ instance.
static_assert(sizeof(MessageLoop) == sizeof(MessageLoopForUI),
              "MessageLoopForUI should not have extra member variables");

#endif  // !defined(OS_NACL)

//-----------------------------------------------------------------------------
// MessageLoopForIO extends MessageLoop with methods that are particular to a
// MessageLoop instantiated with TYPE_IO.
//
// By instantiating a MessageLoopForIO on the current thread, the owner enables
// native async IO message pumping.
//
// MessageLoopCurrentForIO is exposed statically on its thread via
// MessageLoopCurrentForIO::Get() to provide additional functionality.
//
class BASE_EXPORT MessageLoopForIO : public MessageLoop {
 public:
  MessageLoopForIO() : MessageLoop(TYPE_IO) {}
};

// Do not add any member variables to MessageLoopForIO!  This is important b/c
// MessageLoopForIO is often allocated via MessageLoop(TYPE_IO).  Any extra
// data that you need should be stored on the MessageLoop's pump_ instance.
static_assert(sizeof(MessageLoop) == sizeof(MessageLoopForIO),
              "MessageLoopForIO should not have extra member variables");

}  // namespace base

#endif  // BASE_MESSAGE_LOOP_MESSAGE_LOOP_H_

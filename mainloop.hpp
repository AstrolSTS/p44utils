//
//  mainloop.hpp
//  p44utils
//
//  Created by Lukas Zeller on 01.05.13.
//  Copyright (c) 2013 plan44.ch. All rights reserved.
//

#ifndef __p44utils__mainloop__
#define __p44utils__mainloop__

#include "p44_common.hpp"

#include <sys/poll.h>
#include <pthread.h>

using namespace std;

namespace p44 {

  class MainLoop;

  // Mainloop timing unit
  typedef long long MLMicroSeconds;
  const MLMicroSeconds Never = 0;
  const MLMicroSeconds Infinite = -1;
  const MLMicroSeconds MicroSecond = 1;
  const MLMicroSeconds MilliSecond = 1000;
  const MLMicroSeconds Second = 1000*MilliSecond;
  const MLMicroSeconds Minute = 60*Second;

  /// Mainloop callback
  typedef boost::function<bool (MainLoop &aMainLoop, MLMicroSeconds aCycleStartTime)> IdleCB;
  typedef boost::function<void (MainLoop &aMainLoop, MLMicroSeconds aCycleStartTime)> OneTimeCB;


  /// A main loop for a thread
  class MainLoop : public P44Obj
  {

    typedef struct {
      void *subscriberP;
      IdleCB callback;
    } IdleHandler;
    typedef std::list<IdleHandler> IdleHandlerList;

    IdleHandlerList idleHandlers;
    bool idleHandlersChanged;

    typedef struct {
      void *submitterP;
      long ticketNo;
      MLMicroSeconds executionTime;
      OneTimeCB callback;
    } OnetimeHandler;
    typedef std::list<OnetimeHandler> OnetimeHandlerList;

    OnetimeHandlerList onetimeHandlers;
    bool oneTimeHandlersChanged;

    long ticketNo;

  protected:

    bool terminated;

    MLMicroSeconds loopCycleTime;
    MLMicroSeconds cycleStartTime;

    // protected constructor
    MainLoop();

  public:

    /// returns or creates the current thread's mainloop
    static MainLoop &currentMainLoop();

    /// returns the current microsecond
    static MLMicroSeconds now();

    /// set the cycle time
    void setLoopCycleTime(MLMicroSeconds aCycleTime);

    /// get time left for current cycle
    MLMicroSeconds remainingCycleTime();

    /// register routine with mainloop for being called at least once per loop cycle
    /// @param aSubscriberP usually "this" of the caller, or another unique memory address which allows unregistering later
    /// @param aCallback the functor to be called
    void registerIdleHandler(void *aSubscriberP, IdleCB aCallback);

    /// unregister all handlers registered by a given subscriber
    /// @param aSubscriberP a value identifying the subscriber
    void unregisterIdleHandlers(void *aSubscriberP);

    /// have handler called from the mainloop once with an optional delay from now
    /// @param aCallback the functor to be called
    /// @param aExecutionTime when to execute (approximately), in now() timescale
    /// @param aSubmitterP optionally, an identifying value which allows to cancel the pending execution requests
    /// @return ticket number which can be used to cancel this specific execution request
    long executeOnceAt(OneTimeCB aCallback, MLMicroSeconds aExecutionTime, void *aSubmitterP = NULL);

    /// have handler called from the mainloop once with an optional delay from now
    /// @param aCallback the functor to be called
    /// @param aDelay delay from now when to execute (approximately)
    /// @return ticket number which can be used to cancel this specific execution request
    long executeOnce(OneTimeCB aCallback, MLMicroSeconds aDelay = 0, void *aSubmitterP = NULL);

    /// cancel pending execution requests from submitter (NULL = cancel all)
    void cancelExecutionsFrom(void *aSubmitterP);

    /// cancel pending execution by ticket number
    /// @param aTicketNo ticket of execution to cancel. Will be set to 0 on return
    void cancelExecutionTicket(long &aTicketNo);

    /// terminate the mainloop
    void terminate();
    /// run the mainloop
    /// @return returns a exit code
    virtual int run();
  protected:
    // run all handlers
    void runOnetimeHandlers();
    bool runIdleHandlers();
  };


  class SyncIOMainLoop;

  class ChildThreadWrapper;

  typedef enum {
    threadSignalNone,
    threadSignalCompleted, ///< sent to parent when child thread terminates
    threadSignalFailedToStart, ///< sent to parent when child thread could not start
    threadSignalCancelled, ///< sent to parent when child thread was cancelled (
    threadSignalUserSignal ///< first user-specified signal
  } ThreadSignals;


  /// I/O callback
  /// @param aMainLoop the mainloop which calls this handler
  /// @param aCycleStartTime the time when the current mainloop cycle has started
  /// @param aFD the file descriptor that was signalled and has caused this call
  /// @param aPollFlags the poll flags describing the reason for the callback
  /// @return should true if callback really handled some I/O, false if it only checked flags and found nothing to do
  typedef boost::function<bool (SyncIOMainLoop &aMainLoop, MLMicroSeconds aCycleStartTime, int aFD, int aPollFlags)> SyncIOCB;

  /// thread routine, will be called on a separate thread
  /// @param aThreadWrapper the object that wraps the thread and allows sending signals to the parent thread
  ///   Use this pointer to call signalParentThread() on
  /// @note when this routine exits, a threadSignalCompleted will be sent to the parent thread
  typedef boost::function<void (ChildThreadWrapper &aThread)> ThreadRoutine;

  /// thread signal handler, will be called from main loop of parent thread when child thread uses signalParentThread()
  /// @param aMainLoop the mainloop of the parent thread which has started the child thread
  /// @param aChildThread the ChildThreadWrapper object which sent the signal
  /// @param aSignalCode the signal received from the child thread
  typedef boost::function<void (SyncIOMainLoop &aMainLoop, ChildThreadWrapper &aChildThread, ThreadSignals aSignalCode)> ThreadSignalHandler;



  typedef boost::intrusive_ptr<SyncIOMainLoop> SyncIOMainLoopPtr;
  typedef boost::intrusive_ptr<ChildThreadWrapper> ChildThreadWrapperPtr;


  /// A main loop with a Synchronous I/O multiplexer (select() call)
  class SyncIOMainLoop : public MainLoop
  {
    typedef MainLoop inherited;

    typedef struct {
      int monitoredFD;
      int pollFlags;
      SyncIOCB pollHandler;
    } SyncIOHandler;

    typedef std::map<int, SyncIOHandler> SyncIOHandlerMap;
    SyncIOHandlerMap syncIOHandlers;

    // private constructor
    SyncIOMainLoop();

  public:

    /// returns the current thread's SyncIOMainLoop
    /// @return the current SyncIOMainLoop. raises an assertion if current mainloop is not a SyncIOMainLoop
    /// @note creates a SyncIOMainLoop for the thread if no other type of mainloop already exists
    static SyncIOMainLoop &currentMainLoop();

    /// register handler to be called for activity on specified file descriptor
    /// @param aFD the file descriptor to poll
    /// @param aPollFlags POLLxxx flags to specify events we want a callback for
    /// @param aFdEventCB the functor to be called when poll() reports an event for one of the flags set in aPollFlags
    void registerPollHandler(int aFD, int aPollFlags, SyncIOCB aPollEventHandler);

    /// change the poll flags for an already registered handler
    /// @param aFD the file descriptor
    /// @param aPollFlags POLLxxx flags to specify events we want a callback for
    void changePollFlags(int aFD, int aSetPollFlags, int aClearPollFlags=-1);

    /// unregister all handlers registered by a given subscriber
    /// @param aSubscriberP a value identifying the subscriber
    /// @param aFD the file descriptor
    void unregisterPollHandler(int aFD);

    /// execute handler in a separate thread
    /// @param aThreadFunctor the functor to be executed in a separate thread
    /// @param aSubmitterP optionally, an identifying value which allows to cancel the pending execution requests
    /// @return wrapper object for child thread.
    ChildThreadWrapperPtr executeInThread(ThreadRoutine aThreadRoutine, ThreadSignalHandler aThreadSignalHandler);

    /// run the mainloop
    /// @return returns a exit code
    virtual int run();
  protected:
    /// handle IO
    /// @return true if I/O handling occurred
    bool handleSyncIO(MLMicroSeconds aTimeout);

  private:

    void syncIOHandlerForFd(int aFD, SyncIOHandler &h);

  };


  class ChildThreadWrapper : public P44Obj
  {
    typedef P44Obj inherited;

    pthread_t pthread; ///< the pthread
    bool threadRunning; ///< set if thread is active

    SyncIOMainLoop &parentThreadMainLoop; ///< the parent mainloop which created this thread
    int childSignalFd; ///< the pipe used to transmit signals from the child thread
    int parentSignalFd; ///< the pipe monitored by parentThreadMainLoop to get signals from child

    ThreadSignalHandler parentSignalHandler; ///< the handler to call to deliver signals to the main thread
    ThreadRoutine threadRoutine; ///< the actual thread routine to run

    ChildThreadWrapperPtr selfRef;

  public:

    /// constructor
    ChildThreadWrapper(SyncIOMainLoop &aParentThreadMainLoop, ThreadRoutine aThreadRoutine, ThreadSignalHandler aThreadSignalHandler);

    /// destructor
    virtual ~ChildThreadWrapper();

    /// @name methods to call from child thread
    /// @{

    /// signal parent thread
    /// @param aSignalCode a signal code to be sent to the parent thread
    void signalParentThread(ThreadSignals aSignalCode);

    /// @}


    /// @name methods to call from parent thread
    /// @{

    /// cancel execution and wait for cancellation to complete
    void cancel();

    /// confirm termination
    void terminated();

    /// @}

    /// method called from thread_start_function from this child thread
    void *startFunction();

  private:

    bool signalPipeHandler(int aPollFlags);
    void finalizeThreadExecution();

  };


} // namespace p44

#endif /* defined(__p44utils__mainloop__) */

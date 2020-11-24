/**
** \file AsyncDispatcher.hpp
**
** \author Phantomas <phantomas@phantomas.xyz>
** \date Created on: 2020-11-23 23:37
** \date Last update: 2020-11-24 01:58
** \copyright GNU Lesser Public Licence v3
*/

#ifndef ASYNCDISPATCHER_HPP__
#define ASYNCDISPATCHER_HPP__

#include <future>
#include <utility>

#include "./SyncDispatcher.hpp"

namespace clonixin::event {
    /**
    ** \brief Empty class tag type used to specify that the dispatcher should yield when the task list is empty;
    */
    struct yield_t { explicit yield_t() = default; };
    /**
    ** \brief Empty class tag type used to specify that the dispatcher should not yield when the task list is empty.
    */
    struct no_yield_t { explicit no_yield_t() = default; };

    inline constexpr yield_t yield {}; ///< Authorize processing function to yield.
    inline constexpr no_yield_t no_yield {}; ///< Authorize processing function to yield.

    /**
    ** \brief Event Dispatcher class.
    **
    ** This is a generic event Dispatcher. Any function or functor can be registered as an event callback.
    ** Events are identified using their types, using std::type_index internally.
    **
    ** This event dispatcher is meant to be used in an asynchronous way. To do so, user can either call Dispatcher::run, or on of the Dispatcher::process functions.
    ** Calling Dispatcher::run spawn a thread, while Dispatcher::process only process a batch of tasks, before returning.
    **
    ** Thread-safety is ensured by two tasks queue, protected by mutexes. The tasks queues are swapped on call to process,
    ** so processing task won't interfere with pushing new one, and vice versae.
    ** This task system ensure thread-safety in callbackLists, which means that we can dispatch event in a parallel fashion.
    ** Tasks are however processed sequentially.
    **
    ** Tasks include:
    **  - dispatching an event.
    **  - registering a callback.
    **  - unregistering a callback.
    **
    ** \warning Should you use one of the process manually, you should ensure that calling wait on futures returned by registerCallback won't cause any deadlock
    **
    ** \par Callbacks:
    ** \parblock
    **   Callbacks are simple function called when an event is dispatched. They must take one argument, the event, and should not return.
    **
    **
    **   \b Registration: \n
    **      A callback is registered by calling Dispatcher::registerCallback<EvType>\n
    **      This will return a future Handle, that'll be used to unregister that specific callback, if needed.
    **
    **   \b Unregistration: \n
    **      To unregister a callback, you just have to call Dispatcher::unregisterCallback providing it with the necessary handle.
    **
    **   \b Remarks \n
    **      You can't unregister a callback on the same cycle it's added, so several events can be called before you can unregister the callback.
    **      Auto-unregistering callbacks are planned.
    ** \endparblock
    */
    class AsyncDispatcher {
            using _TaskQueue = std::vector<std::packaged_task<void ()>>;
        public:
            AsyncDispatcher();
            AsyncDispatcher(AsyncDispatcher const &) = delete;
            AsyncDispatcher(AsyncDispatcher &&) = delete;
            AsyncDispatcher(SyncDispatcher &&);
            ~AsyncDispatcher();

            AsyncDispatcher &operator=(AsyncDispatcher const &) = delete;
            AsyncDispatcher &operator=(AsyncDispatcher &&) = delete;

            [[nodiscard]] bool run(no_yield_t);
            [[nodiscard]] bool run(yield_t = yield);

            template <class Rep, class Period>
            [[nodiscard]] bool run(std::chrono::duration<Rep, Period> timeout);

            [[nodiscard]] bool stop();

            [[nodiscard]] bool process(no_yield_t t = no_yield);
            [[nodiscard]] bool process(yield_t t);

            template <class EvType>
            AsyncDispatcher &pushEvent(EvType);
            template <class ExecutionPolicy, class EvType>
            AsyncDispatcher &pushEvent(ExecutionPolicy &&policy, EvType ev);

            template <class EvType, typename CallbackFunction>
            [[nodiscard]] std::future<handle> registerCallback(CallbackFunction callback);
            std::future<void> unregisterCallback(handle hndl);
        private:
            template <typename Function>
            bool _startThread(Function &&fun);
            void _process([[maybe_unused]] no_yield_t);
            void _process([[maybe_unused]] yield_t);

            template <class Ret, class Task>
            [[nodiscard]] std::future<Ret> _pushTask(Task &&task);
        private:
            SyncDispatcher _dispatcher; ///< SyncDispatcher used as a backend for dispatching events.

            std::mutex _thread_mu; ///< Protect thread management
            std::atomic_bool _running; ///< Atomic boolean that store the thread status
            std::mutex _process_mu; ///< Prevent two thread from entering concurrently the process function
            std::thread _run_thread; ///< Handle task processing if Dispatcher::run is called,

            std::mutex _tasks_mu; ///< Protect task management
            int _tasks_sel; ///< Simple int to select one task queue or the other
            std::array<_TaskQueue, 2> _tasks; ///< Array of 2 tasks queues, one is always pending (getting filled), while the other is active (getting processed).
    };

    /**
    ** \name Public Member Functions
    */
    /**@{*/
    /**
    ** \brief Default constructor
    */
    AsyncDispatcher::AsyncDispatcher():
        _dispatcher(),
        _thread_mu(), _running(false), _process_mu(), _run_thread(),
        _tasks_mu(), _tasks_sel(0), _tasks()
    {}

    /**
    ** \brief Construct a new AsyncDispatcher by taking a SyncDispatcher rvalue.
    **
    ** \param dispatcher SyncDispatcher to use for task processing.
    */
    AsyncDispatcher::AsyncDispatcher(SyncDispatcher &&dispatcher):
        _dispatcher(std::move(dispatcher)),
        _thread_mu(), _running(false), _process_mu(), _run_thread(),
        _tasks_mu(), _tasks_sel(0), _tasks()
    {}

    AsyncDispatcher::~AsyncDispatcher() {
        (void)stop();
    }
    /**@}*/

    /**
    ** \name Task processing functions.
    */
    /**@{*/
    /**
    ** \brief Start a thread that will process tasks when available.
    **
    ** Calling this function result on a thread to be started, that will process tasks as soon as possible.
    ** The spawn thread will yield if it's has not tasks to run.
    **
    ** If a thread is already running, this function does nothing.
    **
    ** \param t A tag to differentiate it from the non yielding function.
    **
    ** \return \c true if a thread has been spawned, \c false otherwise.
    */
    bool AsyncDispatcher::run(no_yield_t t) {
        auto fun = [&] {
            std::scoped_lock l(_process_mu);
            while (_running.load()) {
                _process(t);
            }
        };

        return _startThread(fun);
    }

    /**
    ** \brief Start a thread that will process tasks when available.
    **
    ** Calling this function result on a thread to be started, that will process tasks as soon as possible.
    **
    ** If a thread is already running, this function does nothing.
    **
    ** \param t A tag to differentiate it from the yielding function.
    **
    ** \return \c true if a thread has been spawned, \c false otherwise.
    */
    bool AsyncDispatcher::run(yield_t t) {
        auto fun = [&] {
            std::scoped_lock l(_process_mu);
            while (_running.load()) {
                _process(t);
            }
        };

        return _startThread(fun);
    }

    /**
    ** \brief Start a thread that will process tasks when available, waiting at least timeout time before swapping process queue.
    **
    ** Calling this function result on a thread to be started, that will process available tasks every \c timeout tick,
    ** sending the thread to sleep between ticks.
    **
    ** If a thread is already running, this function does nothing.
    **
    ** \param [in] timeout Minimum duration of one tick. If the time taken to process task is shorter that duration, the process will sleep for the remainder of the time.
    **
    ** \tparam Rep      Type of the underlying duration representation.
    ** \tparam Period   std::ratio representing the tick period.
    **
    ** \return \c true if a thread has been spawned, \c false otherwise.
    */
    template <class Rep, class Period>
    bool AsyncDispatcher::run(std::chrono::duration<Rep, Period> timeout) {
        auto fun = [&, timeout] {
            auto _start_time = std::chrono::steady_clock::now();
            auto _next_tick = _start_time + timeout;

            std::scoped_lock l(_process_mu);
            for (; _running.load(); _start_time = _start_time + timeout, _next_tick += timeout) {
                _process(no_yield);
                std::this_thread::sleep_until(_next_tick);
            }
        };

        return _startThread(fun);
    }

    /**
    ** \brief Stop any running thread, joining it before returning.
    **
    ** Calling this function when a processing thread is running result in this thread being stopped.
    ** Before stopping, the thread will process remaining threads in the active task queue, but not in the pending one.
    **
    ** If no thread could be stopped (either because _thread_mu was busy, or no thread was running), this function return false.
    **
    ** \return \c true if a thread was running and no other thread was trying to stop it. \c false is returned otherwise.
    */
    bool AsyncDispatcher::stop() {
        std::unique_lock lock(_thread_mu);

        if (!lock || !_running.load())
            return false;

        _running.store(false);
        _run_thread.join();
        _run_thread = std::thread();

        return true;
    }

    /**
    ** \brief Swap tasks queue then process active tasks.
    **
    ** If no other call to process is currently active, this function will swap tasks queue and call every std::packaged_task in the
    ** active queue.
    **
    ** \remarks Internally the function lock a mutex to prevent concurrent calls.
    **
    ** \param t A tag to differentiate it from the non-yielding function.
    **
    ** \return If no other call to process is running, returns \c true. \c false is returned otherwise.
    */
    bool AsyncDispatcher::process(no_yield_t t) {
        std::unique_lock locked(_process_mu, std::try_to_lock);

        if (locked)
            _process(t);

        return bool(locked);
    }

    /**
    ** \brief Swap tasks queue then process active tasks.
    **
    ** If no other call to process is currently active, this function will swap tasks queue and call every std::packaged_task in the
    ** active queue.
    **
    ** If no tasks are contained in the active queue, this function causes the calling thread to yield.
    **
    ** \remarks Internally the function lock a mutex to prevent concurrent calls;
    **
    ** \param t A tag to differentiate it from the yielding function.
    **
    ** \return If no other call to process is running, returns \c true. \c false is returned otherwise.
    */
    bool AsyncDispatcher::process(yield_t t) {
        std::unique_lock locked(_process_mu, std::try_to_lock);


        if (locked)
            _process(t);

        return bool(locked);
    }
    /**@}*/

    /**
    ** \name Event Handling functions
    */
    /**@{*/
    /**
    ** \brief Adds an event dispatching task.
    **
    ** \tparam EvType Type of the event that'll be dispatch on next call to process.
    **
    ** \param [in] ev Event that'll be dispatched.
    **
    ** \return \c *this
    */
    template <class EvType>
    AsyncDispatcher &AsyncDispatcher::pushEvent(EvType ev) {
        auto doDispatch = [&, ev] {
            _dispatcher.dispatch(ev);
        };

        (void)_pushTask<void>(doDispatch);
        return *this;
    }

    /**
    ** \brief Adds an event dispatching task.
    **
    ** The new task will call SyncDispatcher::dispatch(policy, ev);
    **
    ** \tparam ExecutionPolicy Type of execution policy.
    ** \tparam EvType Type of the event that'll be dispatch on next call to process.
    **
    ** \param [in] policy Execution policy to apply.
    ** \param [in] ev Event that'll be dispatched.
    **
    ** \return \c *this
    */
    template <class ExecutionPolicy, class EvType>
    AsyncDispatcher &AsyncDispatcher::pushEvent(ExecutionPolicy &&policy, EvType ev) {
        auto doDispatch = [&, ev, policy] {
            _dispatcher.dispatch(std::move(policy), ev);
        };

        (void)_pushTask<void>(doDispatch);
        return *this;
    }
    /**@}*/

    /**
    ** \name Callback Management
    */
    /**@{*/
    /**
    ** \brief Adds a task to register the callback.
    **
    ** This function adds a new task to register a callback to a type of event, then returns a future on an handle.
    **
    ** \tparam EvType Type of events to register to.
    ** \tparam CallbackFunction Type of the callback. CallbackFunction must be invocable with EvType.
    **
    ** \param [in] callback The callback that'll be called when an event EvType will be dispatched.
    **
    ** \return Returns an std::future<Dispatcher::Handle> that will contains the callback handle when it'll be registered.
    */
    template <class EvType, typename CallbackFunction>
    std::future<handle> AsyncDispatcher::registerCallback(CallbackFunction callback) {
        static_assert(std::is_invocable_v<CallbackFunction, EvType>, "Callback should be invocable with EvType.");

        auto doRegister = [&, callback] {
            return _dispatcher.registeredCallback<EvType>(callback);
        };

        return _pushTask<handle>(doRegister);
    }

    /**
    ** \brief Adds a task to un-register a callback.
    **
    ** This function adds a new task that will unregister the callback represented by \c hndl.
    **
    ** \param [in] hndl A Dispatcher::Handle previously returned by Dispatcher::registerCallback.
    **
    ** \returns A std::future in case something threw in the unregistration process, and the user want to handle the exception.
    */
    std::future<void> AsyncDispatcher::unregisterCallback(handle hndl) {
        std::function<void()> doUnregister = [&, hndl] {
            _dispatcher.unregisterCallback(hndl);
        };

        return _pushTask<void>(doUnregister);
    }
    /**@}*/

    /**
    ** \name Private Member Functions
    */
    /**@{*/
    /**
    ** \brief Private function that start a thread if none were running.
    **
    ** \param [in] fun The routine the thread will run.
    **
    ** \return \c true if a thread was started by this call, \c false otherwise
    */
    template <typename Function>
    bool AsyncDispatcher::_startThread(Function &&fun) {
        std::unique_lock lock(_thread_mu, std::try_to_lock);

        if (!lock || _running.load())
            return false;

        _running.store(true);
        _run_thread = std::thread(std::forward<Function>(fun));

        return true;
    }

    /**
    ** \brief Actual process implementation.
    **
    ** This function locks the tasks mutex long enough to swap the tasks queues.
    ** Once done, the old pending task queue become the active one.
    ** That queue then get processed by invoking all packaged_task it contains.
    **
    ** \param t A tag to differentiate it from the yielding function.
    **
    ** This function does not returns.
    */
    void AsyncDispatcher::_process([[maybe_unused]] no_yield_t t) {
        int sel;
        {
            std::scoped_lock sl(_tasks_mu);
            sel = _tasks_sel;
            _tasks_sel ^= 1;
        }

        std::for_each(_tasks[sel].begin(), _tasks[sel].end(), [](auto &task) {
            task();
        });

        _tasks[sel].clear();
    }

    /**
    ** \brief Actual processOrYield implementation.
    **
    ** This function locks the tasks mutex long enough to swap the tasks queues.
    ** Once done, the old pending task queue become the active one.
    ** That queue then get processed by invoking all packaged_task it contains.
    ** If no tasks are contained in the active task queue, the calling thread is caused to yield.
    **
    ** \param t A tag to differentiate it from the non yielding function.
    **
    ** This function does not returns.
    */
    void AsyncDispatcher::_process([[maybe_unused]] yield_t t) {
        int sel;
        {
            std::scoped_lock sl(_tasks_mu);
            sel = _tasks_sel;
            _tasks_sel ^= 1;
        }

        if (0 != _tasks[sel].size())
            std::for_each(_tasks[sel].begin(), _tasks[sel].end(), [](auto &task) { task(); });
        else
            std::this_thread::yield();

        _tasks[sel].clear();
    }

    /**
    ** \brief Adds a tasks to the pending tasks queue.
    **
    ** \tparam Ret The type that should be returned by the task.
    ** \tparam Task Type of the task that will be run.
    **
    ** \param [in] fun The function that will be called when the task is processed.
    **
    ** \return Returns a std::future<Ret> that will contains the content of the task when it'll be executed.
    */
    template <class Ret, class Task>
    std::future<Ret> AsyncDispatcher::_pushTask(Task &&fun) {
        static_assert(std::is_invocable_r_v<Ret, Task>, "Task take should return Ret when and be invocable without argument.");
        std::packaged_task<Ret()> task(std::forward<Task>(fun));
        auto future = task.get_future();

        {
            std::scoped_lock sl(_tasks_mu);
            _tasks[_tasks_sel & 1].emplace_back(std::move(task));
        }

        return future;
    }
    /**@}*/
}

#endif

/**
** \file Dispatcher.hpp
**
** \author Phantomas <phantomas@phantomas.xyz>
** \date Created on: 2020-11-03 12:22
** \date Last update: 2020-11-03 12:22
** \copyright GNU Lesser Public Licence v3
*/

#ifndef DISPATCHER_HPP__
#define DISPATCHER_HPP__

#include <any>
#include <chrono>
#include <execution>
#include <functional>
#include <future>
#include <queue>
#include <stdexcept>
#include <typeinfo>
#include <typeindex>
#include <unordered_map>

namespace clonixin::event::async {

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
    class Dispatcher {

        /**
        ** \name Member Types
        */
        /**@{*/
        /**
        ** \brief Metadatas for callback handling
        */
        struct _TypeMeta {
            size_t id_list = 0; ///< This Id is used to assign an ID to a callback
            std::function<void(size_t)> unreg; ///< Callback unregistration function.
        };

        /**
        ** \brief Refer to a registered callback.
        **
        ** This should be used as an opaque type.
        */
        struct Handle {
            std::type_index const type; ///< std::type_index to know which callback_list the referred callback is in.
            size_t const id; ///< Id of the callback.
        };

        using _TaskQueue = std::vector<std::packaged_task<void ()>>;

        template <class EvType>
            using _CallbackWrap = std::pair<size_t, std::function<void(EvType)>>;

        template <class EvType>
            using _CallbackList = std::vector<_CallbackWrap<EvType>>;
        /**@}*/
        public:
            Dispatcher();
            ~Dispatcher();

            bool run();
            template <class Rep, class Period>
            bool run(std::chrono::duration<Rep, Period> timeout);
            bool stop();

            bool process();
            bool processOrYield();

            template <class EvType>
            Dispatcher &pushEvent(EvType);

            template <class EvType>
            Dispatcher const &dispatch(EvType event) const;

            template <class EvType>
            Dispatcher &dispatch(EvType event);

            template <class EvType>
            Dispatcher const &dispatchPar(EvType event) const;

            template <class EvType>
            Dispatcher &dispatchPar(EvType event);

            template <class EvType, typename CallbackFunction>
            [[nodiscard]]
            std::future<Handle> registerCallback(CallbackFunction callback);
            std::future<void> unregisterCallback(Handle hndl);

        private:
            bool _startThread(std::function<void()> fun);
            void _process();
            void _processOrYield();

            template <class EvType>
            void _dispatch(EvType event) const;

            template <class EvType>
            void _dispatchPar(EvType event) const;

            template <class Ret, class Task>
            [[nodiscard]]
            std::future<Ret> _pushTask(Task task);

            template <class EvType, typename CallbackFunction>
            [[nodiscard]]
            Handle _doRegister(CallbackFunction callback);


            std::mutex _thread_mu; ///< Protect thread management
            std::atomic_bool _running; ///< Atomic boolean that store the thread status
            std::mutex _process_mu; ///< Prevent two thread from entering concurrently the process function
            std::thread _run_thread; ///< Handle task processing if Dispatcher::run is called,

            std::mutex _tasks_mu; ///< Protect task management
            int _tasks_sel; ///< Simple int to select one task queue or the other
            std::array<_TaskQueue, 2> _tasks; ///< Array of 2 tasks queues, one is always pending (getting filled), while the other is active (getting processed).

            std::unordered_map<std::type_index, std::any> _callback_list; ///< Map of vector of callback, indexed by event types.
            std::unordered_map<std::type_index, _TypeMeta> _meta_list; ///< Map of metadata, indexed by event types.
    };

    /**
    ** \name Public Member Functions
    */
    /**@{*/
    /**
    ** \brief Construct a new Dispatcher
    */
    Dispatcher::Dispatcher()
    :   _thread_mu(), _running(false),
        _tasks_mu(), _tasks_sel(0), _tasks(),
        _callback_list(), _meta_list()
    {}

    /**
    ** \brief Destructs a Dispatcher, stopping it's internal thread if needed.
    **
    ** \warning Calling the destructor while tasks are pending will result in all futures to throw an exceptions,
    ** as their corresponding packaged_task will get destroyed.
    */
    Dispatcher::~Dispatcher() {
        stop();
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
    ** \return \c true if a thread has been spawned, \c false otherwise.
    */
    bool Dispatcher::run() {
        auto fun = [&] {
            std::scoped_lock l(_process_mu);
            while (_running.load()) {
                _processOrYield();
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
    bool Dispatcher::run(std::chrono::duration<Rep, Period> timeout) {
        auto fun = [&, timeout] {
            auto _start_time = std::chrono::steady_clock::now();
            auto _next_tick = _start_time + timeout;

            std::scoped_lock l(_process_mu);
            for (; _running.load(); _start_time = _start_time + timeout, _next_tick += timeout) {
                _process();
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
    bool Dispatcher::stop() {
        std::unique_lock lock(_thread_mu);

        if (!lock || !_running.load())
            return false;

        _running.store(false);
        _run_thread.join();
        _run_thread = std::thread();

        return true;
    }

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
    Dispatcher &Dispatcher::pushEvent(EvType ev) {
        auto doDispatch = [&, ev] {
            dispatchPar(ev);
        };

        (void)_pushTask<void>(doDispatch);
        return *this;
    }

    /**
    ** \brief Swap tasks queue then process active tasks.
    **
    ** If no other call to process is currently active, this function will swap tasks queue and call every std::packaged_task in the
    ** active queue.
    **
    ** \remarks Internally the function lock a mutex to prevent concurrent calls.
    **
    ** \return If no other call to process is running, returns \c true. \c false is returned otherwise.
    */
    bool Dispatcher::process() {
        std::unique_lock locked(_process_mu, std::try_to_lock);

        if (locked)
            _process();

        return bool(locked);
    }

    /**
    ** \brief Actual process implementation.
    **
    ** This function locks the tasks mutex long enough to swap the tasks queues.
    ** Once done, the old pending task queue become the active one.
    ** That queue then get processed by invoking all packaged_task it contains.
    **
    ** This function does not returns.
    */
    void Dispatcher::_process() {
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
    ** \brief Swap tasks queue then process active tasks.
    **
    ** If no other call to process is currently active, this function will swap tasks queue and call every std::packaged_task in the
    ** active queue.
    **
    ** If no tasks are contained in the active queue, this function causes the calling thread to yield.
    **
    ** \remarks Internally the function lock a mutex to prevent concurrent calls;
    **
    ** \return If no other call to process is running, returns \c true. \c false is returned otherwise.
    */
    bool Dispatcher::processOrYield() {
        std::unique_lock locked(_process_mu, std::try_to_lock);

        if (locked)
            _processOrYield();

        return bool(locked);
    }

    /**
    ** \brief Actual processOrYield implementation.
    **
    ** This function locks the tasks mutex long enough to swap the tasks queues.
    ** Once done, the old pending task queue become the active one.
    ** That queue then get processed by invoking all packaged_task it contains.
    ** If no tasks are contained in the active task queue, the calling thread is caused to yield.
    **
    ** This function does not returns.
    */
    void Dispatcher::_processOrYield() {
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
    /**@}*/

    /**
    ** \name Event Handling functions
    */
    /**@{*/
    /**
    ** \brief Synchronously dispatch an event.
    **
    ** Sequentially calls the dispatched event callbacks.
    **
    ** \warning This function should not be called if another thread is currently calling one of the Dispatcher::process* function.
    **
    ** \tparam EvType Type of the event to dispatch
    **
    ** \param [in] ev Event to dipatch
    **
    ** \return \c *this
    */
    template <class EvType>
    Dispatcher const & Dispatcher::dispatch(EvType ev) const {
        _dispatch(ev);

        return *this;
    }

    /**
    ** \brief Synchronously dispatch an event.
    **
    ** Sequentially calls the dispatched event callbacks.
    **
    ** \warning This function should not be called if another thread is currently calling one of the Dispatcher::process* function.
    **
    ** \tparam EvType Type of the event to dispatch
    **
    ** \param [in] ev Event to dipatch
    **
    ** \return \c *this
    */
    template <class EvType>
    Dispatcher & Dispatcher::dispatch(EvType ev) {
        _dispatch(ev);

        return *this;
    }

    /**
    ** \brief Synchronously dispatch an event.
    **
    ** Calls the callback for the dispatched event. Function calls happens in parallel.
    **
    ** \warning This function should not be called if another thread is currently calling one of the Dispatcher::process* function.
    **
    ** \tparam EvType Type of the event to dispatch
    **
    ** \param [in] ev Event to dipatch
    **
    ** \return \c *this
    */
    template <class EvType>
    Dispatcher const & Dispatcher::dispatchPar(EvType ev) const {
        _dispatchPar(ev);

        return *this;
    }

    /**
    ** \brief Synchronously dispatch an event.
    **
    ** Calls the callback for the dispatched event. Function calls happens in parallel.
    **
    ** \warning This function should not be called if another thread is currently calling one of the Dispatcher::process* function.
    **
    ** \tparam EvType Type of the event to dispatch
    **
    ** \param [in] ev Event to dipatch
    **
    ** \return \c *this
    */
    template <class EvType>
    Dispatcher & Dispatcher::dispatchPar(EvType ev) {
        _dispatchPar(ev);

        return *this;
    }

    /**@}*/

    /**
    ** \name Callback Management
    ** \internal
    */
    /**@{*/

    /**
    ** \brief Synchronously dispatch an event.
    **
    ** Sequentially calls the dispatched event callbacks.
    **
    ** \warning This function should not be called if another thread is currently calling one of the Dispatcher::process* function.
    **
    ** \tparam EvType Type of the event to dispatch
    **
    ** \param [in] ev Event to dipatch
    */
    template <class EvType>
    void Dispatcher::_dispatch(EvType ev) const {
        try {
            std::type_index ti(typeid(EvType));
            _CallbackList<EvType> list = std::any_cast<_CallbackList<EvType>>(_callback_list.at(ti));

            auto unar = [&, ev] (_CallbackWrap<EvType> meta) {
                auto [id, fun] = meta;
                fun(ev);
            };

            std::for_each(list.begin(), list.end(), unar);
        } catch (std::out_of_range) {}
    }

    /**
    ** \brief Synchronously dispatch an event.
    **
    ** Calls the callback for the dispatched event. Function calls happens in parallel.
    **
    ** \warning This function should not be called if another thread is currently calling one of the Dispatcher::process* function.
    **
    ** \tparam EvType Type of the event to dispatch
    **
    ** \param [in] ev Event to dipatch
    */
    template <class EvType>
    void Dispatcher::_dispatchPar(EvType ev) const {
        try {
            std::type_index ti(typeid(EvType));
            _CallbackList<EvType> list = std::any_cast<_CallbackList<EvType>>(_callback_list.at(ti));

            auto unar = [&, ev] (_CallbackWrap<EvType> meta) {
                auto [id, fun] = meta;
                fun(ev);
            };

            std::for_each(std::execution::par, list.begin(), list.end(), unar);
        } catch (std::out_of_range) {}
    }

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
    std::future<Dispatcher::Handle> Dispatcher::registerCallback(CallbackFunction callback) {
        static_assert(std::is_invocable_v<CallbackFunction, EvType>, "Callback should be invocable with EvType.");

        auto doRegister = [&, callback] {
            return _doRegister<EvType>(callback);
        };

        return _pushTask<Handle>(doRegister);
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
    std::future<void> Dispatcher::unregisterCallback(Handle hndl) {
        std::function<void()> doUnregister = [&, hndl] {
            try {
                auto meta = _meta_list.at(hndl.type);
                meta.unreg(hndl.id);
            } catch (std::out_of_range) {}
        };

        return _pushTask<void>(doUnregister);
    }
    /**@}*/

    /**
    ** \name Private member functions
    */
    /**@{*/
    /**
    ** \brief Private function that start a thread if none were running.
    **
    ** \param [in] fun The routine the thread will run.
    **
    ** \return \c true if a thread was started by this call, \c false otherwise
    */
    bool Dispatcher::_startThread(std::function<void()> fun) {
        std::unique_lock lock(_thread_mu, std::try_to_lock);

        if (!lock || _running.load())
            return false;

        _running.store(true);
        _run_thread = std::thread(fun);

        return true;
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
    std::future<Ret> Dispatcher::_pushTask(Task fun) {
        static_assert(std::is_invocable_r_v<Ret, Task>, "Task take should return Ret when and be invocable without argument.");
        std::packaged_task<Ret()> task(fun);
        auto future = task.get_future();

        {
            std::scoped_lock sl(_tasks_mu);
            _tasks[_tasks_sel & 1].emplace_back(std::move(task));
        }

        return future;
    }

    /**
    ** \brief Register a callback to an event.
    **
    ** If needed, this function create a new metadata to the type of event. \n
    ** It then adds a callback to the callback_list.
    **
    ** \tparam EvType Type of event to register the callback to.
    ** \tparam CallbackFunction This type should be invocable with EvType.
    **
    ** \param callback Callback to register. This will be called when an event of type EvType will be raised.
    **
    ** \return Dispatcher::Handle that enable management of callback.
    */
    template <class EvType, typename CallbackFunction>
    Dispatcher::Handle Dispatcher::_doRegister(CallbackFunction callback) {
        static_assert(std::is_invocable_v<CallbackFunction, EvType>, "Callback should be invocable with EvType.");
        std::type_index ti(typeid(EvType));

        static auto unregister = [&, ti] (size_t id) {
            auto & list = std::any_cast<_CallbackList<EvType>&>(_callback_list[ti]);

            auto pos = std::remove_if(list.begin(), list.end(), [&](_CallbackWrap<EvType> wrap) {
                    auto [_id, _] = wrap;
                    return id == _id;
            });
            list.erase(pos);
        };
        _meta_list.try_emplace(ti, _TypeMeta{0, unregister});
        _callback_list.try_emplace(ti, _CallbackList<EvType>{});

        size_t id = _meta_list[ti].id_list++;
        auto &cb_list = std::any_cast<_CallbackList<EvType>&>(_callback_list[ti]);
        cb_list.emplace_back(id, callback);

        return Handle{ti, id};
    }
    /**@}*/
}

#endif

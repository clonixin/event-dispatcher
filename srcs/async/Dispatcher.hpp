/**
** \file Dispatcher.hpp
**
** \author Phantomas <phantomas@phantomas.xyz>
** \date Created on: 2020-11-03 12:22
** \date Last update: 2020-11-03 12:22
** \copyright GNU Lesser Public Licence v3
*/

#include <any>
#include <chrono>
#include <execution>
#include <functional>
#include <future>
#include <queue>
#include <stdexcept>
#include <typeinfo>
#include <typeindex>

namespace clonixin::event::async {
    class Dispatcher {
        struct _TypeMeta {
            size_t id_list = 0;
            std::function<void(size_t)> unreg;
        };

        struct Handle {
            std::type_index const type;
            size_t const id;
        };
        public:
            Dispatcher();
            ~Dispatcher();

            bool run();
            template <class Rep, class Period>
            void run(std::chrono::duration<Rep, Period> timeout);
            bool stop();

            template <class EvType>
            void pushEvent(EvType);

            void process();
            void processOrYield();

            template <class EvType>
            void dispatch(EvType event) const;
            template <class EvType>
            void dispatchPar(EvType event) const;

            template <class EvType>
            [[nodiscard]]
            std::future<Handle> registerCallback(std::function<void(EvType)> callback);
            std::future<void> unregisterCallback(Handle hndl);

        private:
            using TaskQueue = std::vector<std::packaged_task<void ()>>;

            template <class EvType>
            using _CallbackWrap = std::pair<size_t, std::function<void(EvType)>>;

            template <class EvType>
            using _CallbackList = std::vector<_CallbackWrap<EvType>>;

            bool _startThread(std::function<void()> fun);

            template <class RetType>
            [[nodiscard]]
            std::future<RetType> _pushTask(std::function<RetType()> task);

            template <class EvType>
            [[nodiscard]]
            Handle _doRegister(std::function<void(EvType)> callback);


            std::mutex _stop_mu;
            std::atomic_bool _running;
            std::atomic_bool _early_stop;
            std::thread _run_thread;

            std::recursive_mutex _tasks_mu;
            int _tasks_sel;
            std::array<TaskQueue, 2> _tasks;

            std::unordered_map<std::type_index, std::any> _callback_list;
            std::unordered_map<std::type_index, _TypeMeta> _meta_list;
    };

    Dispatcher::Dispatcher()
    :   _stop_mu(), _running(false), _early_stop(false),
        _tasks_mu(), _tasks_sel(0), _tasks(),
        _callback_list(), _meta_list()
    {}

    Dispatcher::~Dispatcher() {
        stop();
    }


    bool Dispatcher::run() {
        auto fun = [&] {
            while (_running.load()) {
                processOrYield();
            }
        };

        return _startThread(fun);
    }

    template <class Rep, class Period>
    void Dispatcher::run(std::chrono::duration<Rep, Period> timeout) {
        auto fun = [&, timeout] {
            auto _start_time = std::chrono::steady_clock::now();
            auto _next_tick = _start_time + timeout;

            for (; _running.load(); _start_time += timeout, _next_tick += timeout) {
                process();
                std::this_thread::sleep_until(_next_tick);
            }
        };

        return _startThread(fun);
    }


    bool Dispatcher::stop() {
        std::unique_lock lock(_stop_mu);
        bool locked = lock.try_lock();

        if (!locked || !_running.load())
            return false;

        _running.store(false);
        _run_thread = std::thread();

        return true;
    }

    template <class EvType>
    void Dispatcher::pushEvent(EvType ev) {
        auto doDispatch = [&, ev] {
            dispatchPar(ev);
        };

        return _pushTask(doDispatch);
    }

    void Dispatcher::process() {
        int sel;
        {
            std::scoped_lock sl(_tasks_mu);
            sel = _tasks_sel;
            _tasks_sel ^= 1;
        }

        std::for_each(_tasks[sel].begin(), _tasks[sel].end(), [](auto task) { task(); });
    }

    void Dispatcher::processOrYield() {
        int sel;
        {
            std::scoped_lock sl(_tasks_mu);
            sel = _tasks_sel;
            _tasks_sel ^= 1;
        }

        if (0 != _tasks[sel].size())
            std::for_each(_tasks[sel].begin(), _tasks[sel].end(), [](auto task) { task(); });
        else
            std::this_thread::yield();
    }

    template <class EvType>
    void Dispatcher::dispatch(EvType ev) const {
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

    template <class EvType>
    void Dispatcher::dispatchPar(EvType ev) const {
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

    template <class EvType>
    std::future<Dispatcher::Handle> Dispatcher::registerCallback(std::function<void(EvType)> callback) {
        auto doRegister = [&, callback] {
            return _doRegister(callback);
        };

        return _pushTask(doRegister);
    }

    std::future<void> Dispatcher::unregisterCallback(Handle hndl) {
        std::function<void()> doUnregister = [&, hndl] {
            try {
                auto meta = _meta_list.at(hndl.type);
                meta.unreg(hndl.id);
            } catch (std::out_of_range) {}
        };

        return _pushTask(doUnregister);
    }

    bool Dispatcher::_startThread(std::function<void()> fun) {
        std::unique_lock lock(_stop_mu);
        bool locked = lock.try_lock();

        if (!locked || !_running.load())
            return false;

        _running.store(true);
        _run_thread = std::thread(fun);

        return true;
    }

    template <class Ret>
    std::future<Ret> Dispatcher::_pushTask(std::function<Ret()> fun) {
        std::packaged_task<Ret()> task(fun);
        auto future = task.get_future();

        {
            std::scoped_lock sl(_tasks_mu);
            _tasks[_tasks_sel & 1].emplace_back(std::move(task));
        }

        return future;
    }

    template <class EvType>
    Dispatcher::Handle Dispatcher::_doRegister(std::function<void(EvType)> callback) {
        std::type_index ti(typeid(EvType));

        auto unregister = [&, ti] (size_t id) {
            auto & list = std::any_cast<_CallbackList<EvType>>(_callback_list[ti]);

            auto pos = std::find_if(list.begin(), list.end(), [&](_CallbackWrap<EvType> wrap) {
                    auto [_id, _] = wrap;
                    return id == _id;
            });
            list.erase(pos);
        };
        _meta_list.try_emplace(ti, {0, unregister});

        size_t id = _meta_list[ti].id_list++;
        _callback_list[ti] = {id, callback};

        return Handle{ti, id};
    }
}

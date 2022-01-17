/**
** \file SyncDispatcher.hpp
**
** \author Phantomas <phantomas@phantomas.xyz>
** \date Created on: 2020-11-22 23:08
** \date Last update: 2022-01-11 11:46
** \copyright GNU Lesser Public Licence v3
*/

#ifndef SYNCDISPATCHER_HPP__
#define SYNCDISPATCHER_HPP__

#include <any>
#include <functional>
#include <stdexcept>
#include <typeinfo>
#include <typeindex>
#include <unordered_map>

namespace clonixin::events {
    /**
    ** \brief Refer to a registered callback.
    **
    ** This should be used as an opaque type.
    */
    struct handle {
        std::type_index const type; ///< std::type_index to know which callback_list the referred callback is in.
        size_t const id; ///< Id of the callback.
    };

    const handle nil_handle { typeid(nullptr), (size_t)-1 };

    bool operator==(handle const & lhs, handle const &rhs) {
        return lhs.type == rhs.type && lhs.id == rhs.id;
    }

    bool operator!=(handle const & lhs, handle const &rhs) {
        return !(lhs == rhs);
    }

    /*
    ** \brief Event Dispatcher class.
    **
    ** This is a generic event Dispatcher. Any function or functor can be registered as an event callback.
    ** Events are identified using their types, using std::type_index internally.
    **
    ** \par Callbacks:
    ** \parblock
    **   Callbacks are simples functions called when an event is dispatched. They must take one argument, the event, and should not return.
    **
    **   \b Registration: \n
    **      A callback is registered by calling SyncDispatcher::registerCallback<EvType>\n
    **      This will return a event::handle, that'll be used to unregister that specific callback, if needed.
    **
    **   \b Unregistration: \n
    **      To unregister a callback, you just have to call SyncDispatcher::unregisterCallback providing it with the necessary handle.
    */
    class SyncDispatcher {
        /**
        ** \brief Metadatas for callback handling
        */
        struct _TypeMeta {
            size_t id_list = 0; ///< This Id is used to assign an ID to a callback
            std::function<void(size_t, std::any&)> unreg; ///< Callback unregistration function.
        };

        /**
        ** \name Member Types
        */
        /**@{*/
        template <class EvType>
            using _CallbackWrap = std::pair<size_t, std::function<void(EvType)>>;

        template <class EvType>
            using _CallbackList = std::vector<_CallbackWrap<EvType>>;
        /**@}*/
        public:
            SyncDispatcher();
            SyncDispatcher(SyncDispatcher &&) noexcept;
            ~SyncDispatcher();

            SyncDispatcher & operator=(SyncDispatcher &&) noexcept;

            template <class EvType>
            SyncDispatcher & dispatch(EvType ev);
            template <class EvType>
            SyncDispatcher const & dispatch(EvType ev) const;

            template <class ExecutionPolicy, class EvType>
            SyncDispatcher & dispatch(ExecutionPolicy && policy, EvType ev);
            template <class ExecutionPolicy, class EvType>
            SyncDispatcher const & dispatch(ExecutionPolicy && policy,EvType ev) const;

            template <class EvType, typename CallbackFunction>
            [[nodiscard]] handle registerCallback(CallbackFunction callback);
            SyncDispatcher &unregisterCallback(handle hndl);

        private:
            std::unordered_map<std::type_index, std::any> _callback_list; ///< Map of vector of callback, indexed by event types.
            std::unordered_map<std::type_index, _TypeMeta> _meta_list; ///< Map of metadata, indexed by event types.
    };

    /**
    ** \name Public Member Functions
    */
    /**@{*/
    SyncDispatcher::SyncDispatcher() {}
    SyncDispatcher::SyncDispatcher(SyncDispatcher && oth) noexcept
    : _callback_list(std::move(oth._callback_list)), _meta_list(std::move(oth._meta_list)) {}

    SyncDispatcher::~SyncDispatcher() {}

    SyncDispatcher & SyncDispatcher::operator=(SyncDispatcher &&oth) noexcept {
        if (std::addressof(oth) != this) {
            _callback_list = std::move(oth._callback_list);
            _meta_list = std::move(oth._meta_list);
        }

        return *this;
    }
    /**@}*/

    /**
    ** \name Event Handling functions
    */
    /**@{*/
    /**
    ** \brief Dispatch an event.
    **
    ** Sequentially calls the dispatched event callbacks.
    **
    ** \tparam EvType Type of the event to dispatch
    **
    ** \param [in] ev Event to dipatch
    **
    ** \return \c *this
    */
    template <class EvType>
    SyncDispatcher & SyncDispatcher::dispatch(EvType ev) {
        ((SyncDispatcher const *)this)->dispatch(ev);
        return *this;
    }

    /**
    ** \brief Dispatch an event.
    **
    ** Sequentially calls the dispatched event callbacks.
    **
    ** \tparam EvType Type of the event to dispatch
    **
    ** \param [in] ev Event to dipatch
    **
    ** \return \c *this
    */
    template <class EvType>
    SyncDispatcher const & SyncDispatcher::dispatch(EvType ev) const {
        try {
            std::type_index ti(typeid(EvType));
            _CallbackList<EvType> const &list = std::any_cast<_CallbackList<EvType> const &>(_callback_list.at(ti));

            auto unar = [&, ev] (_CallbackWrap<EvType> meta) {
                auto [id, fun] = meta;
                fun(ev);
            };

            std::for_each(list.begin(), list.end(), unar);
        } catch (std::out_of_range) {}

        return *this;
    }

    /**
    ** \brief Dispatch an event according to a specific execution policy.
    **
    ** Calls the dispatched event callbacks according to the specified policy.
    ** `std::for_each` is called internally, thus, if you want to use custom policies,
    ** you have to customize std::for_each accordingly.
    **
    ** \tparam ExecutionPolicy Type of execution policy.
    ** \tparam EvType Type of the event to dispatch
    **
    ** \param [in] policy Execution policy to apply.
    ** \param [in] ev Event to dipatch
    **
    ** \return \c *this
    */
    template <class ExecutionPolicy, class EvType>
    SyncDispatcher & SyncDispatcher::dispatch(ExecutionPolicy && policy, EvType ev) {
        ((SyncDispatcher const *)this)->dispatch(policy, ev);
        return *this;
    }

    /**
    ** \brief Dispatch an event according to a specific execution policy.
    **
    ** Calls the dispatched event callbacks according to the specified policy.
    ** `std::for_each` is called internally, thus, if you want to use custom policies,
    ** you have to customize std::for_each accordingly.
    **
    ** \tparam ExecutionPolicy Type of execution policy.
    ** \tparam EvType Type of the event to dispatch
    **
    ** \param [in] policy Execution policy to apply.
    ** \param [in] ev Event to dipatch
    **
    ** \return \c *this
    */
    template <class ExecutionPolicy, class EvType>
    SyncDispatcher const & SyncDispatcher::dispatch(ExecutionPolicy && policy,EvType ev) const {
        try {
            std::type_index ti(typeid(EvType));
            _CallbackList<EvType> list = std::any_cast<_CallbackList<EvType>>(_callback_list.at(ti));

            auto unar = [&, ev] (_CallbackWrap<EvType> meta) {
                auto [id, fun] = meta;
                fun(ev);
            };

            std::for_each(std::forward(policy), list.begin(), list.end(), unar);
        } catch (std::out_of_range) {}

        return *this;
    }
    /**@}*/

    /**
    ** \name Callback Management
    */
    /**@{*/
    /**
    ** \brief Register a callback to a given event..
    **
    ** This function register a callback to a type of event, then returns a handle that enable handling of the event.
    **
    ** \tparam EvType Type of events to register to.
    ** \tparam CallbackFunction Type of the callback. CallbackFunction must be invocable with EvType.
    **
    ** \param [in] callback The callback that'll be called when an event EvType will be dispatched.
    **
    ** \return Returns a events::handle that will allow to unregister the event..
    */
    template <class EvType, typename CallbackFunction>
    handle SyncDispatcher::registerCallback(CallbackFunction callback) {
        static_assert(std::is_invocable_v<CallbackFunction, EvType>, "Callback should be invocable with EvType.");
        std::type_index ti(typeid(EvType));

        static auto unregister = [&, ti] (size_t id, std::any & callbacks) {
            auto & list = std::any_cast<_CallbackList<EvType>&>(callbacks);

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

        return handle{ti, id};
    }

    /**
    ** \brief Unregister a callback.
    **
    ** This function unregister the callback represented by \c hndl.
    **
    ** \param [in] hndl A event::handle previously returned by SyncDispatcher::registerCallback.
    **
    ** \return \c *this.
    */
    SyncDispatcher & SyncDispatcher::unregisterCallback(handle hndl) {
        try {
            auto meta = _meta_list.at(hndl.type);
            meta.unreg(hndl.id, _callback_list.at(hndl.type));
        } catch (std::out_of_range) {}

        return *this;
    }
    /**@}*/
}

#endif


/**
** \file Dispatcher.hpp
**
** \author Phantomas <phantomas@phantomas.xyz>
** \date Created on: 2020-10-20 13:52
** \date Last update: 2020-10-20 13:52
** \copyright GNU Lesser Public Licence v3
*/

#include <any>
#include <functional>
#include <stdexcept>
#include <typeinfo>
#include <typeindex>

namespace clonixin::event {
    /**
    ** \brief Clonixin's event Dispatcher
    **
    ** This is a generic event dispatcher. It aim to be easy to use.
    ** Any function or functor can be registered to a specific type of events.
    ** Instead of using an identifier (ID or Name), the data structure used to contains event data
    ** is used as the event identifier.
    **
    ** To use it, you first have to register your callbacks using the registerCallback<> method.
    ** This function is templated on the type of event you want to register to.
    **
    ** Once registered, callbacks will be called when the dispatchSync<> method is called.
    **
    ** Planed features:
    ** - Add a way to auto unregister a given callback if it's handle is destroyed.
    ** - Add async dispatch.
    */
    class Dispatcher {
            struct _CallbackHandle {
                std::type_index type;
                size_t id;
            };
        public:
            using handle = _CallbackHandle;
        public:

            Dispatcher() {}
            ~Dispatcher() {}

            template <class EvType>
            void dispatchSync(EvType &&event) const;

            template <class EvType>
            handle registerCallback(std::function<void(EvType)> callback);

            void unregisterCallback(handle && hndl);

        private:
            using callbackPair = std::pair<size_t, std::any>;
            using callbackList = std::vector<callbackPair>;

            std::unordered_map<std::type_index, callbackList> _callbacks;
            std::unordered_map<std::type_index, size_t> _id_list;
    };

    /**
    ** \brief Dispatch a given event to all registered callback.
    **
    ** \param event Event data.
    **
    ** \tparam EvType Type that will contains the event data.
    */
    template <class EvType>
    void Dispatcher::dispatchSync(EvType &&event) const {
        auto unar = [&] (callbackPair pair) {
            auto [id, fun] = pair;
            std::any_cast<std::function<void(EvType)>>(fun)(event);
        };

        try {
            std::vector const callbacks = _callbacks.at(std::type_index(typeid(EvType)));
            std::for_each(callbacks.begin(), callbacks.end(), unar);
        } catch (std::out_of_range) {}
    }

    /**
    ** \brief Register a callback to the dispatcher.
    **
    ** \param callback A function or functor wrapped in an std::function<void (EvType)>
    **
    ** \tparam EvType Type of event this function is to be registered to.
    **
    ** \return This function return a handle, to be able to manage the callback.
    */
    template <class EvType>
    Dispatcher::handle Dispatcher::registerCallback(std::function<void(EvType)> callback) {
        std::type_index const idx(typeid(EvType));
        _id_list.try_emplace(idx, 0);

        handle const hndl {idx, ++(_id_list[idx])};

        _callbacks[idx].push_back({hndl.id, callback});
        return hndl;
    }

    /**
    ** \brief Unregister a callback to the dispatcher.
    **
    ** \param hndl Handle to identify the callback to be removed.
    */
    void Dispatcher::unregisterCallback(handle && hndl) {
        auto & list = _callbacks[hndl.type];
        auto pos = find_if(list.begin(), list.end(), [&](callbackPair pair) {
            return hndl.id == pair.first;
        });

        list.erase(pos);
    }
}


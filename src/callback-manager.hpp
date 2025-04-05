#pragma once

#include <functional>
#include <unordered_map>


// Generic type for C-style callback with last user argument (void *)
template<typename ReturnType, typename... Args>
using CStyleCallbackType = ReturnType (*)(Args..., void*);


template<typename ReturnType, typename... Args>
class CStyleCallbackManager {

    struct Metadata {
        CStyleCallbackManager& manager;
        void* userData() { return static_cast<void*>(this); }
    };

public:
    void* operator()(std::function<ReturnType(Args...)>&& callback) {
        if (!callback)
            return nullptr;
        auto key = new Metadata({*this});
        callbacks[key->userData()] = std::move(callback);
        return key->userData();
    }

    void remove(void* userData) {
        if (userData)
            callbacks.erase(userData);
    }

    void erase(void* userData) {
        if (userData)
            callbacks.erase(userData);
    }

private:
    std::unordered_map<void*, std::function<ReturnType(Args...)>> callbacks;

    static ReturnType callbackFunction(Args... args, void* userData) {
        if (userData != nullptr) {
            auto& metadata = *static_cast<Metadata*>(userData);
            auto it = metadata.manager.callbacks.find(userData /* or metadata.userData() */);
            if (it != metadata.manager.callbacks.end())
                return it->second(args...);
        }
        throw std::invalid_argument("callback function not found"); // TODO: add type here
    }

public:
    inline static CStyleCallbackType<ReturnType, Args...> callback = &CStyleCallbackManager::callbackFunction;
};

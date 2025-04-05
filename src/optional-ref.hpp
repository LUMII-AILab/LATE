#pragma once

#include <optional>


template<typename T>
class optional_ref {
public:
    optional_ref() : ref_(nullptr) {}
    optional_ref(std::nullopt_t) : ref_(nullptr) {}
    optional_ref(T& ref) : ref_(&ref) {}

    bool has_value() const { return ref_ != nullptr; }

    operator bool() const { return ref_ != nullptr; }

    T& value() {
        if (!ref_) throw std::bad_optional_access();
        return *ref_;
    }

    const T& value() const {
        if (!ref_) throw std::bad_optional_access();
        return *ref_;
    }

    T& operator*() { return value(); }
    const T& operator*() const { return value(); }

    T* operator->() { return &value(); }
    const T* operator->() const { return &value(); }

private:
    T* ref_;
};


/*
// usage example

#include <iostream>

class WhisperJobInternal {
public:
    WhisperJobInternal(int id) : id(id) {}
    int id;
};

optional_ref<WhisperJobInternal> getWhisperJob(bool returnValid) {
    static WhisperJobInternal instance(42); // Static instance of WhisperJobInternal
    if (returnValid) {
        return optional_ref<WhisperJobInternal>(instance); // Return reference to the static instance
    } else {
        return optional_ref<WhisperJobInternal>(); // Return empty optional_ref
    }
}


int main() {
    auto optJob = getWhisperJob(true);
    if (optJob.has_value()) {
        std::cout << "Received valid reference with id: " << optJob->id << std::endl;
    } else {
        std::cout << "Received nullopt" << std::endl;
    }

    auto optJobNull = getWhisperJob(false);
    if (optJobNull.has_value()) {
        std::cout << "Received valid reference with id: " << optJobNull->id << std::endl;
    } else {
        std::cout << "Received nullopt" << std::endl;
    }

    return 0;
}
*/

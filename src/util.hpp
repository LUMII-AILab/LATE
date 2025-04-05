#pragma once

#include <memory>

template <typename T>
class SharedBuffer {
    size_t _size = 0;
    const T* _data = nullptr;
public:
    SharedBuffer() { }
    SharedBuffer(const std::shared_ptr<T>& shared_ptr, size_t size) : ptr_manager(shared_ptr), _data(shared_ptr.get()), _size(size) {}
    // SharedBuffer(T* ptr, size_t size) : ptr_manager(ptr), data(ptr), size(size) {}
    SharedBuffer(T* ptr, size_t size, bool owner = true) : _data(ptr), _size(size) { if (owner) ptr_manager.reset(ptr); }
    template <typename Deleter>
    SharedBuffer(T* ptr, Deleter d, size_t size) : ptr_manager(ptr, d), _data(ptr), _size(size) {}
    SharedBuffer(SharedBuffer&& other) : ptr_manager(std::move(other.ptr_manager)), _data(other._data), _size(other._size), size(_size), count(_size), data(_data) {}
    bool empty() const { return data == nullptr || size == 0; }
    void free() { if (ptr_manager) ptr_manager.reset(); _data = nullptr; _size = 0; }
    const size_t& size = _size;
    const size_t& count = _size;
    const T*& data = _data;
    const T* dat() const { return _data; };
private:
    std::shared_ptr<T> ptr_manager;
};


// template <typename T>
// class SharedBuffer {
// public:
//     SharedBuffer() { }
//     // SharedBuffer(T* ptr, size_t size) : ptr_manager(ptr), data(ptr), size(size) {}
//     SharedBuffer(T* ptr, size_t size, bool owner = true) : data(ptr), size(size) { if (owner) ptr_manager.reset(ptr); }
//     template <typename Deleter>
//     SharedBuffer(T* ptr, Deleter d, size_t size) : ptr_manager(ptr, d), data(ptr), size(size) {}
//     bool empty() const { return data == nullptr || size == 0; }
//     const size_t size = 0;
//     const size_t& count = size;
//     const T* data = nullptr;
// private:
//     std::shared_ptr<T> ptr_manager;
// };

// template <typename T>
// class SharedBuffer {
// public:
//     SharedBuffer() { }
//     // SharedBuffer(T* ptr, size_t size) : ptr_manager(ptr), data(ptr), size(size) {}
//     SharedBuffer(T* ptr, size_t size, bool owner = true) : _data(ptr), _size(size) { if (owner) ptr_manager.reset(ptr); }
//     template <typename Deleter>
//     SharedBuffer(T* ptr, Deleter d, size_t size) : ptr_manager(ptr, d), _data(ptr), _size(size) {}
//     bool empty() const { return _data == nullptr || _size == 0; }
//     size_t size() const { return _size; }
//     const T* data() const { return _data; }
// private:
//     std::shared_ptr<T> ptr_manager;
//     size_t _size = 0;
//     T* _data = nullptr;
// };

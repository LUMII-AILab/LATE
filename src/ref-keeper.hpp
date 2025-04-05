#pragma once


template <typename T>
class ReferenceKeeper {
public:
    // template <typename T>
    class Keeper {
        ReferenceKeeper<T>* prk = nullptr;
    public:
        Keeper(ReferenceKeeper<T>& rk) : prk(&rk) { (*prk)++; }
        Keeper(ReferenceKeeper<T>* prk) : prk(prk) { (*prk)++; }
        Keeper(const Keeper& other) : prk(other.prk) { (*prk)++; }
        Keeper(Keeper&& other) : prk(other.prk) { other.prk = nullptr; }
        ~Keeper() { if (prk) (*prk)--; }
        void operator=(Keeper&& other) { prk = other.prk; other.prk = nullptr; }
        void operator=(const Keeper& other) { prk = other.prk; (*prk)++; }
        void operator=(ReferenceKeeper<T>& rk) { prk = &rk; (*prk)++; }
        void operator=(ReferenceKeeper<T>* prk) { prk = prk; (*prk)++; }
        T& value() const { return prk->value(); }
        T& ref() const { return prk->ref(); }
        T* get() const { return prk->get(); }
        T* pointer() const { return prk->pointer(); }
    };
    ReferenceKeeper() { ptr = nullptr; count = 0; }
    ReferenceKeeper(T *ptr) { set(ptr); }
    void set(T *ptr) { this->ptr = ptr; count = 0; }
    void reset(T *ptr) { this->ptr = ptr; count = 0; }
    void operator++() { count++; }
    void operator++(int) { count++; }
    void operator--() { count--; if (count == 0) ptr = nullptr; }
    void operator--(int) { count--; if (count == 0) ptr = nullptr; }
    void operator=(T* ptr) { set(ptr); }
    void operator=(T& ref) { set(&ref); }
    operator bool() const { return ptr != nullptr; }
    T& value() const { return *ptr; }
    T& ref() const { return *ptr; }
    T* get() const { return ptr; }
    T* pointer() const { return ptr; }
    Keeper scoped() { return std::move(Keeper(this)); }
private:
    T* ptr = nullptr;
    int count = 0;
};

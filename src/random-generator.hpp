#pragma once

#include <random>
#include <mutex>
#include <optional>


// thread safe random string generator
class RandomStringGenerator {
public:
    using seed_type = std::mt19937::result_type;

    static constexpr seed_type default_seed = std::mt19937::default_seed;
    static inline const std::string default_charset = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

    RandomStringGenerator(size_t default_length = 0, std::optional<seed_type> seed = std::nullopt, const std::string& charset = default_charset)
        : default_length(default_length), charset(charset), rnd_gen(seed.value_or(rd())), charset_rnd_distrib(0, charset.size() - 1) {}

    std::string operator()(size_t length = 0) {
        if (length == 0)
            length = default_length;
        std::string randomID;
        if (length == 0)
            return randomID;
        std::lock_guard<std::mutex> lock(mtx);
        std::generate_n(std::back_inserter(randomID), length, [&]() {
            return charset[charset_rnd_distrib(rnd_gen)];
        });
        return randomID;
    }

    void seed(std::optional<seed_type> value = std::nullopt) { rnd_gen.seed(value.value_or(rd())); }

    const std::string charset;
    const size_t default_length;

private:
    std::mutex mtx;
    std::random_device rd;
    std::mt19937 rnd_gen;
    std::uniform_int_distribution<int> charset_rnd_distrib;
};


// thread safe random int generator
template <typename IntType = int>
class RandomIntGenerator {
public:
    using seed_type = std::mt19937::result_type;

    static constexpr seed_type default_seed = std::mt19937::default_seed;

    RandomIntGenerator(std::optional<seed_type> seed = std::nullopt, IntType min = std::numeric_limits<IntType>::lowest(), IntType max = std::numeric_limits<IntType>::max())
        : min(min), max(max), rnd_gen(seed.value_or(rd())), rnd_distrib(min, max) {}

    IntType operator()() {
        std::lock_guard<std::mutex> lock(mtx);
        return rnd_distrib(rnd_gen);
    }

    void seed(std::optional<seed_type> value = std::nullopt) { rnd_gen.seed(value.value_or(rd())); }

    const IntType min;
    const IntType max;
private:
    std::mutex mtx;
    std::random_device rd;
    std::mt19937 rnd_gen;
    std::uniform_int_distribution<IntType> rnd_distrib;
};


// thread safe random floating-point generator
template <typename RealType = double>
class RandomRealGenerator {
public:
    using seed_type = std::mt19937::result_type;

    static constexpr seed_type default_seed = std::mt19937::default_seed;

    RandomRealGenerator(std::optional<seed_type> seed = std::nullopt, RealType min = std::numeric_limits<RealType>::lowest(), RealType max = std::numeric_limits<RealType>::max())
        : min(min), max(max), rnd_gen(seed.value_or(rd())), rnd_distrib(min, max) {}

    RealType operator()() {
        std::lock_guard<std::mutex> lock(mtx);
        return rnd_distrib(rnd_gen);
    }

    void seed(std::optional<seed_type> value = std::nullopt) { rnd_gen.seed(value.value_or(rd())); }

    const RealType min;
    const RealType max;
private:
    std::mutex mtx;
    std::random_device rd;
    std::mt19937 rnd_gen;
    std::uniform_real_distribution<RealType> rnd_distrib;
};


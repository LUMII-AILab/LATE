#pragma once

#include <string>
#include <map>
#include <stdexcept>
#include <initializer_list>
#include <functional>
#include <iterator>

#include "string_util.hpp"
#include "utf8_util.hpp"


using EngineID = int;

enum class Engines : EngineID {
    Whisper
};

/*
// Usage:
EngineDeviceConfigurations engineDeviceConf;
// add engines
engineDeviceConf.add(Engines::Whisper, 0, "whisper", {"w", "asr"});
engineDeviceConf.add(Engines::TTS, 0, "tts", {"tts", "vits"});

// set device to specified for each engine, defaulting to gpu:auto if no device number specified
engineDevices.apply("whisper:cpu,tts:gpu#1", 0, EngineDeviceConfigurations::ImplicitOverride::Allowed)
// set device to cpu for each engine specified
engineDevices.apply("whisper,tts", -1, EngineDeviceConfigurations::ImplicitOverride::NotAllowed)
*/

class EngineDeviceConfigurations {

public:
    typedef EngineID id_type;
    typedef int device_type;

    enum class ImplicitOverride {
        NotAllowed, // implicit value acts as hardcoded value - used to set hardcoded device to each engine specified, e.g., for --cpu=... option
        Allowed,    // implicit value acts as default value - used to set specific device number, but default to auto, e.g., for --gpu=... option
        Required    // no effect of implicit - used to set device explicitly for each engine specified, e.g., for --device=... option
    };
    // device numbers:
    // -1 - CPU
    // 0 or *, default, auto, any - GPU with auto or default device number
    // 1, 2, 3, ... - explicitly select GPU device (1 based)

    EngineDeviceConfigurations(device_type device = 0) : allEngines(AllEngines, "all", device) {
        allEngines.setter = [&](device_type device) { this->setAll(device); };
        aliases["all"] = AllEngines;
        aliases["any"] = AllEngines;
        aliases["a"] = AllEngines;
        aliases["*"] = AllEngines;
    }

    void add(Engines engine, device_type defaultDevice, std::string name, std::initializer_list<std::string> aliases) {
        id_type id = static_cast<id_type>(engine);
        engines.try_emplace(id, id, name, defaultDevice);
        this->aliases[name] = id;
        for (auto it = aliases.begin(); it != aliases.end(); ++it) {
            this->aliases[*it] = id;
        }
    }

    bool apply(const std::string& config, int implicitDeviceNumber /* type is inferred */ = -1, ImplicitOverride implicitOverrideSetting = ImplicitOverride::Allowed) {
        for (auto& engineConfig : split(toLowerCase(config), ",")) {
            if (engineConfig.empty())
                continue;
            auto parts = split(engineConfig, ":");
            if (parts.size() == 0)
                // throw std::invalid_argument("no engine specified")
                continue; // probably comma separated emtpy string
            else if (parts.size() > 3)
                throw std::invalid_argument("invalid engine device setting " + engineConfig);
            try {
                auto& engine = engineForAlias(parts[0]);
                if (parts.size() == 1) {
                    // engine name only
                    if (implicitOverrideSetting == ImplicitOverride::Required)
                        throw std::invalid_argument("missing device for engine " + engine.name);
                    engine.set(implicitDeviceNumber);
                } else if (parts.size() == 2) {
                    // engine name with device setting
                    if (implicitOverrideSetting == ImplicitOverride::NotAllowed)
                        throw std::invalid_argument("device setting not allowed (hardcoded to " + std::to_string(implicitDeviceNumber) + ") for engine " + engine.name);
                    auto setting = parts[1];
                    parts = split(setting, "#");
                    if (parts.size() == 1) {
                        // either device type name or device number
                        if (parts[0] == "cpu")
                            engine.set(-1); // CPU
                        else if (parts[0] == "gpu")
                            engine.set(0);  // GPU: default/auto device
                        else {
                            try {
                                int device = std::stoi(parts[0]);
                                if (device < 0)
                                    engine.set(-1);
                                engine.set(device);
                            } catch(const std::out_of_range& e) {
                                throw std::invalid_argument("invalid device setting for engine " + engine.name + ": " + setting);
                            }
                        }
                    } else if (parts.size() == 2) {
                        // must be device type name with device number
                        if (parts[0] == "cpu")
                            throw std::invalid_argument("CPU device does not take device number for engine " + engine.name);
                        else if (parts[0] == "gpu") {
                            try {
                                int device;
                                if (parts[1] == "*" || parts[1] == "default" || parts[1] == "any" || parts[1] == "auto")
                                    device = 0;
                                else {
                                    device = std::stoi(parts[1]);
                                    if (device < 0)
                                        throw std::invalid_argument("invalid GPU device number for engine " + engine.name + ": " + parts[1]);
                                }
                                engine.set(device);
                            } catch(const std::out_of_range& e) {
                                throw std::invalid_argument("invalid device setting for engine " + engine.name + ": " + setting);
                            }
                        } else
                            throw std::invalid_argument("unknown device type for engine " + engine.name + ": " + parts[0]);
                    } else
                        throw std::invalid_argument("invalid device setting for engine " + engine.name + ": " + setting);
                } else if (parts.size() == 3) {
                    // engine:type:number, e.g., whisper:gpu:0
                    if (parts[1] == "cpu")
                        throw std::invalid_argument("CPU device does not take device number for engine " + engine.name);
                    else if (parts[1] == "gpu") {
                        int device;
                        if (parts[1] == "*" || parts[1] == "default" || parts[1] == "any" || parts[1] == "auto")
                            device = 0;
                        else {
                            device = std::stoi(parts[2]);
                            if (device < 0)
                                throw std::invalid_argument("invalid GPU device number for engine " + engine.name + ": " + parts[2]);
                        }
                        engine.set(device);
                    } else
                        throw std::invalid_argument("unknown device type for engine " + engine.name + ": " + parts[1]);
                }
            } catch(const std::out_of_range& e) {
                throw std::invalid_argument("engine " + parts[0] + " was not recognized");
            } catch(const std::invalid_argument& e) {
                throw;
            }
        }
        return true;
    }

    device_type operator[](const id_type id) const { return engines.at(id).device; }
    device_type operator[](const Engines id) const { return engines.at(static_cast<id_type>(id)).device; }

    bool IsGPU(const id_type id) const { return engines.at(id).device >= 0; }
    bool IsGPU(const Engines id) const { return engines.at(static_cast<id_type>(id)).device >= 0; }

private:
    typedef struct EngineDeviceConfiguration {
        id_type id;
        std::string name;
        device_type device = 0;
        EngineDeviceConfiguration(id_type id, const std::string& name, device_type device) : id(id), name(name), device(device) {}
        void set(device_type device) { if (setter) setter(device); else this->device = device; }
        std::function<void(device_type)> setter;
    } EngineDeviceConfiguration;
public:

    class Iterator;

    class Engine {
    public:
        const Engines engine;
        const std::string& name;
        const device_type device;
        std::string deviceString() {
            if (device == -1)
                return "cpu";
            else if (device >= 0)
                return std::string("gpu#") + std::to_string(device);
            return "unknown";
        }
    private:
        Engine(const EngineDeviceConfiguration& e) : engine(static_cast<Engines>(e.id)), name(e.name), device(e.device) {}
        friend class Iterator;
    };


    class Iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = Engine;
        using pointer = Engine*;
        using reference = Engine&;
        using difference_type = std::ptrdiff_t;

        value_type operator*() const { return Engine(it->second); }
        // pointer operator->() const { return Engine(it->second); }

        Iterator& operator++() {
            ++it;
            return *this;
        }

        Iterator operator++(int) {
            Iterator temp(it);
            ++temp.it;
            return temp;
        }

        friend bool operator==(const Iterator& a, const Iterator& b) {
            return a.it == b.it;
        }

        friend bool operator!=(const Iterator& a, const Iterator& b) {
            return a.it != b.it;
        }

    private:
        std::map<id_type, EngineDeviceConfiguration>::iterator it;

        Iterator(std::map<id_type, EngineDeviceConfiguration>::iterator&& it) : it(it) {}
        Iterator(const std::map<id_type, EngineDeviceConfiguration>::iterator& it) : it(it) {}
        friend class EngineDeviceConfigurations;
    };

    Iterator begin() { return Iterator(engines.begin()); }
    Iterator end() { return Iterator(engines.end()); }

private:
    const id_type AllEngines = -1;


    EngineDeviceConfiguration& operator[](const std::string& alias) {
        auto idx = aliases.at(alias);
        return engines.at(idx);
    }

    EngineDeviceConfiguration& engineForAlias(const std::string& alias) {
        int id = aliases.at(alias);
        if (id == AllEngines)
            return allEngines;
        return engines.at(id);
    }

    void setAll(device_type device) {
        for (auto& [id, engine] : engines) {
            engine.set(device);
        }
    }

    EngineDeviceConfiguration allEngines;

    std::map<std::string, id_type> aliases;
    std::map<id_type, EngineDeviceConfiguration> engines;
};



#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <map>

// Base interface for all VanGoghFlow Wayland visualizer plugins
class Plugin {
public:
    virtual ~Plugin() = default;

    // Identifying name (e.g. "starfield", "matrix", "aurora", "fireflies")
    virtual std::string name() const = 0;

    // Human-readable description
    virtual std::string description() const = 0;

    // Lifecycle hooks
    virtual void init(int width, int height) = 0;
    virtual void resize(int width, int height) = 0;
    virtual void render(float dt, float master_opacity) = 0;
    virtual void cleanup() = 0;
};

// Central registry for compiled-in plugins
class PluginRegistry {
public:
    using Factory = std::function<std::unique_ptr<Plugin>()>;

    struct PluginInfo {
        std::string name;
        std::string description;
        Factory factory;
    };

    static PluginRegistry& instance() {
        static PluginRegistry reg;
        return reg;
    }

    void register_plugin(const std::string& name, const std::string& desc, Factory factory) {
        plugins_[name] = {name, desc, std::move(factory)};
    }

    std::unique_ptr<Plugin> create(const std::string& name) const {
        auto it = plugins_.find(name);
        if (it != plugins_.end()) {
            return it->second.factory();
        }
        return nullptr;
    }

    std::vector<PluginInfo> list() const {
        std::vector<PluginInfo> result;
        for (const auto& pair : plugins_) {
            result.push_back(pair.second);
        }
        return result;
    }

    bool has(const std::string& name) const {
        return plugins_.find(name) != plugins_.end();
    }

private:
    PluginRegistry() = default;
    std::map<std::string, PluginInfo> plugins_;
};

// Helper macro for auto-registering plugins at startup
#define REGISTER_PLUGIN(Class, Name, Desc) \
    namespace { \
        struct Class##Registrar { \
            Class##Registrar() { \
                PluginRegistry::instance().register_plugin(Name, Desc, []() { \
                    return std::make_unique<Class>(); \
                }); \
            } \
        }; \
        static Class##Registrar _auto_register_##Class; \
    }

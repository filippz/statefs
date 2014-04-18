/**
 * @file util.cpp
 * @brief Internal utilities used by statefs server
 *
 * @author (C) 2012, 2013 Jolla Ltd. Denis Zalevskiy <denis.zalevskiy@jollamobile.com>
 * @copyright LGPL 2.1 http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html
 */

#include "statefs.hpp"
#include <statefs/util.h>

#include <boost/filesystem.hpp>
#include <iostream>
#include <string>

bool ensure_dir_exists(std::string const &dir_name)
{
    namespace fs = boost::filesystem;
    if (fs::exists(dir_name)) {
        if (!fs::is_directory(dir_name)) {
            std::cerr << dir_name << " should be directory" << std::endl;
            return false;
        }
    } else {
        if (!fs::create_directory(dir_name)) {
            std::cerr << "Can't create dir " << dir_name << std::endl;
            return false;
        }
    }
    return true;
}

namespace statefs {

ns_handle_type mk_namespace_handle(statefs_namespace *ns)
{
    auto ns_release = [](statefs_namespace *p)
        {
            if (p)
                statefs_node_release(&p->node);
        };
    return ns_handle_type(ns, ns_release);
}

property_handle_type mk_property_handle(statefs_property *p)
{
    auto release = [](statefs_property *p)
        {
            if (p)
                statefs_node_release(&p->node);
        };
    return property_handle_type(p, release);
}


namespace server {

using statefs::provider_ptr;

LoaderProxy::LoaderProxy(std::string const& path)
    : lib_(path, RTLD_LAZY)
    , impl_(std::move(create(lib_)))
{
}

provider_ptr LoaderProxy::load(std::string const& path, statefs_server *server)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return impl_ ? impl_->load(path, server) : nullptr;
}

std::string LoaderProxy::name() const
{
    return impl_ ? impl_->name() : "";
}

bool LoaderProxy::is_reloadable() const
{
    return impl_ ? impl_->is_reloadable() : true;
}

bool LoaderProxy::is_valid() const
{
    return !!impl_;
}

LoaderProxy::impl_ptr LoaderProxy::create(cor::SharedLib &lib)
{
    if (!lib.is_loaded()) {
        std::cerr << "Lib loading error " << ::dlerror() << std::endl;
        return nullptr;
    }
    auto fn = lib.sym<create_provider_loader_fn>
        (statefs::cpp_loader_accessor());
    if (!fn) {
        trace() << "Can't resolve "
                  << statefs::cpp_loader_accessor() << std::endl;
        return nullptr;
    }

    auto loader = fn();
    if (!loader) {
        std::cerr << "provider is null" << std::endl;
    } else if (!statefs_is_version_compatible
               (STATEFS_CPP_LOADER_VERSION, loader->version())) {
        std::cerr << "statefs: Incompatible loader version "
                  << loader->version() << " vs "
                  << STATEFS_CPP_LOADER_VERSION;
        return nullptr;
    }
    return impl_ptr(loader);
}

bool LoadersStorage::loader_register(loader_info_ptr info)
{
    auto name = info->value();
    auto p = loaders_.find(name);
    if (p != loaders_.end()) {
        if (!p->second->is_reloadable()) {
            std::cerr << "Loader " << name
                      << " can't be replaced now, skipping" << std::endl;
            return false;
        }
        std::cerr << "Replacing existing loader " << name << std::endl;
        loaders_.erase(p);
    }

    info_.insert(std::make_pair(name, info));
    return true;
}

std::shared_ptr<LoaderProxy> LoadersStorage::loader_get(std::string const& name)
{
    auto it = loaders_.find(name);
    if (it == loaders_.end()) {
        auto pinfo = info_.find(name);
        if (pinfo == info_.end())
            return nullptr;

        auto path = pinfo->second->path;
        auto loader = std::make_shared<LoaderProxy>(path);
        auto added = loaders_.insert(std::make_pair(name, loader));
        return (added.first)->second;
    };
    return it->second;
}

bool LoadersStorage::loader_rm(std::string const& name)
{
    auto p = loaders_.find(name);
    // if some provider is holding reference to the loader 
    if (p != loaders_.end()) {
        auto loader = p->second;
        if (!loader->is_reloadable()) {
            std::cerr << "Loader " << name
                      << " can't be removed now, skipping" << std::endl;
            return false;
        }

        if (!loader.unique())
            std::cerr << "Loader " << name
                      << " is still used" << std::endl;

        loaders_.erase(p);
        info_.erase(name);
        return true;
    }
    return false;
}

}}

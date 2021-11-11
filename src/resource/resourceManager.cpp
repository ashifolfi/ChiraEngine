#include "resourceManager.h"

using namespace chira;

void resourceManager::addResourceProvider(const std::string& name, abstractResourceProvider* provider) {
    if (resourceManager::providers.find(name) == resourceManager::providers.end()) {
        resourceManager::providers[name] = std::vector<std::unique_ptr<abstractResourceProvider>>{};
    }
    resourceManager::providers[name].emplace_back(provider);
}

abstractResourceProvider* resourceManager::getLatestResourceProvider(const std::string& provider) {
    return resourceManager::providers[provider][resourceManager::providers[provider].size() - 1].get();
}

abstractResourceProvider* resourceManager::getResourceProviderWithResource(const std::string& identifier) {
    auto id = resourceManager::splitResourceIdentifier(identifier);
    const std::string& provider = id.first, name = id.second;
    for (auto i = resourceManager::providers[provider].rbegin(); i < resourceManager::providers[provider].rend(); i++) {
        if (i->get()->hasResource(name)) {
            return i->get();
        }
    }
    logger::log(ERR, "Resource Manager", fmt::format(TR("error.resource_manager.resource_not_found"), identifier));
    return nullptr;
}

std::pair<std::string,std::string> resourceManager::splitResourceIdentifier(const std::string& identifier) {
    std::pair<std::string,std::string> out;
    size_t pos = identifier.find(RESOURCE_ID_SEPARATOR);
    if (pos != std::string::npos) {
        out.first = identifier.substr(0, pos);
        out.second = identifier.substr(pos + RESOURCE_ID_SEPARATOR.length());
    } else {
        logger::log(ERR, "resourceManager::splitResourceIdentifier", fmt::format(TR("error.resource_manager.cannot_split_identifier"), identifier));
    }
    return out;
}

void resourceManager::removeResource(const std::string& identifier) {
    auto id = resourceManager::splitResourceIdentifier(identifier);
    const std::string& provider = id.first, name = id.second;
    // If the count is <= 2, then it's being held by the resource manager and the object requesting its removal.
    if (resourceManager::resources[provider][name].use_count() <= 2) {
        resourceManager::resources[provider].erase(name);
    }
}

void resourceManager::discardAll() {
    // Make sure all resources are deleted, just in case
    for (const auto& [providerName, resourceMap] : resources) {
        for (const auto& [name, resource] : resourceMap) {
            delete resource.get();
        }
    }
    resourceManager::resources.clear();
    resourceManager::providers.clear();
}

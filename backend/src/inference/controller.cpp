#include <stdexcept>

#include "dualview/inference.hpp"
namespace dualview::inference {
void Registry::add(std::string id, Factory factory) {
  if (id.empty() || !factory || !factories_.emplace(std::move(id), std::move(factory)).second)
    throw std::invalid_argument("Duplicate or empty inference provider");
}
std::unique_ptr<Provider> Registry::select(const std::string& id) const {
  const auto it = factories_.find(id);
  if (it == factories_.end()) throw std::invalid_argument("Unknown inference provider: " + id);
  auto result = it->second();
  if (!result) throw std::runtime_error("Provider factory returned no instance: " + id);
  if (!result->info().available) throw ProviderUnavailable("Inference provider unavailable: " + id);
  return result;
}
std::vector<ProviderInfo> Registry::available() const {
  std::vector<ProviderInfo> result;
  for (const auto& [id, factory] : factories_) {
    auto provider = factory();
    if (!provider) throw std::runtime_error("Provider factory returned no instance: " + id);
    result.push_back(provider->info());
  }
  return result;
}
Controller::Controller(const Registry& registry, const std::string& requested, bool fallback) {
  try {
    provider_ = registry.select(requested);
  } catch (const std::invalid_argument&) {
    throw;
  } catch (const ProviderUnavailable& e) {
    if (!fallback || requested == "cpu") throw;
    fallback_reason_ = e.what();
    provider_ = registry.select("cpu");
  }
}
std::unique_ptr<Model> Controller::load(const std::filesystem::path& path) const {
  return provider_->load(path);
}
ProviderInfo Controller::info() const { return provider_->info(); }
}  // namespace dualview::inference

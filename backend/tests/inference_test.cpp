#include "dualview/inference.hpp"

#include <cassert>
#include <iostream>
using namespace dualview::inference;
class Unavailable : public Provider {
  ProviderInfo info() const override { return {"unavailable", "test", false}; }
  std::unique_ptr<Model> load(const std::filesystem::path&) const override {
    throw std::runtime_error("unavailable");
  }
};
int main() {
  auto registry = default_registry();
  registry.add("unavailable", [] { return std::make_unique<Unavailable>(); });
  bool rejected = false;
  try {
    Controller c(registry, "unavailable", false);
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  assert(rejected);
  Controller fallback(registry, "unavailable", true);
  assert(fallback.info().id == "cpu");
  assert(!fallback.fallback_reason().empty());
  rejected = false;
  try {
    Controller c(registry, "typo", true);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  assert(rejected);
  registry.add("broken", []() -> std::unique_ptr<Provider> {
    throw std::runtime_error("Provider initialization defect");
  });
  rejected = false;
  try {
    Controller c(registry, "broken", true);
  } catch (const std::runtime_error& e) {
    rejected = std::string(e.what()) == "Provider initialization defect";
  }
  assert(rejected);
  registry.add("null", []() -> std::unique_ptr<Provider> { return nullptr; });
  rejected = false;
  try {
    registry.select("null");
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  assert(rejected);
  rejected = false;
  try {
    registry.add("", [] { return std::make_unique<Unavailable>(); });
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  assert(rejected);
  std::cout << "Provider selection and explicit fallback verified\n";
}

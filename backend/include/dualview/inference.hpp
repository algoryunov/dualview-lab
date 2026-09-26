#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
namespace dualview::inference {
struct TensorContract {
  std::string name;
  std::vector<std::int64_t> shape;
};
struct TensorOutput {
  std::vector<std::int64_t> shape;
  std::vector<float> values;
};
class Model {
 public:
  virtual ~Model() = default;
  virtual const TensorContract& input() const = 0;
  virtual const std::vector<TensorContract>& outputs() const = 0;
  virtual std::vector<TensorOutput> run(const std::vector<float>& values) const = 0;
};
struct ProviderInfo {
  std::string id;
  std::string device;
  bool available;
};
class Provider {
 public:
  virtual ~Provider() = default;
  virtual ProviderInfo info() const = 0;
  virtual std::unique_ptr<Model> load(const std::filesystem::path& path) const = 0;
};
class ProviderUnavailable : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};
using Factory = std::function<std::unique_ptr<Provider>()>;
class Registry {
 public:
  void add(std::string id, Factory factory);
  std::unique_ptr<Provider> select(const std::string& id) const;
  std::vector<ProviderInfo> available() const;

 private:
  std::map<std::string, Factory> factories_;
};
Registry default_registry();
// One controller owns the selected provider. The runtime executes at most one
// frame pair at a time and replaces queued input with the newest frames.
class Controller {
 public:
  Controller(const Registry& registry, const std::string& requested, bool allow_cpu_fallback);
  std::unique_ptr<Model> load(const std::filesystem::path& path) const;
  ProviderInfo info() const;
  const std::string& fallback_reason() const { return fallback_reason_; }

 private:
  std::unique_ptr<Provider> provider_;
  std::string fallback_reason_;
};
}  // namespace dualview::inference

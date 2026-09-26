#include <algorithm>
#include <cstdint>
#include <numeric>
#include <stdexcept>

#include "dualview/inference.hpp"

#ifdef __APPLE__
#include <coreml_provider_factory.h>
#endif
#include <onnxruntime_cxx_api.h>

namespace dualview::inference {

class OnnxModel final : public Model {
 public:
  OnnxModel(const std::filesystem::path&, const std::string&);
  ~OnnxModel() override;
  const TensorContract& input() const override;
  const std::vector<TensorContract>& outputs() const override;
  std::vector<TensorOutput> run(const std::vector<float>&) const override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
namespace {

std::size_t element_count(const std::vector<std::int64_t>& shape) {
  if (shape.empty() ||
      std::ranges::any_of(shape, [](const auto dimension) { return dimension <= 0; })) {
    throw std::runtime_error("The hand models must have fully static, positive tensor shapes.");
  }
  return std::accumulate(shape.begin(), shape.end(), std::size_t{1},
                         [](const auto total, const auto dimension) {
                           return total * static_cast<std::size_t>(dimension);
                         });
}

TensorContract tensor_contract(const Ort::Session& session, const std::size_t index,
                               const bool input) {
  const auto type_info = input ? session.GetInputTypeInfo(index) : session.GetOutputTypeInfo(index);
  const auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
  if (tensor_info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
    throw std::runtime_error("The selected hand models must use float tensors.");
  }
  return {input ? session.GetInputNameAllocated(index, Ort::AllocatorWithDefaultOptions{}).get()
                : session.GetOutputNameAllocated(index, Ort::AllocatorWithDefaultOptions{}).get(),
          tensor_info.GetShape()};
}

}  // namespace

struct OnnxModel::Impl {
  Ort::Env environment{ORT_LOGGING_LEVEL_WARNING, "dualview-native"};
  Ort::Session session{nullptr};
  TensorContract input_contract;
  std::vector<TensorContract> output_contracts;
  std::vector<const char*> input_names;
  std::vector<const char*> output_names;
};

OnnxModel::OnnxModel(const std::filesystem::path& model_path, const std::string& provider)
    : impl_(std::make_unique<Impl>()) {
  if (!std::filesystem::is_regular_file(model_path)) {
    throw std::runtime_error("ONNX model file does not exist: " + model_path.string());
  }

  Ort::SessionOptions options;
  options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
  options.SetIntraOpNumThreads(1);
  if (provider == "coreml") {
#ifdef __APPLE__
    Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_CoreML(
        options, COREML_FLAG_ONLY_ALLOW_STATIC_INPUT_SHAPES));
#else
    throw std::runtime_error("Core ML requires macOS");
#endif
  }
  impl_->session = Ort::Session(impl_->environment, model_path.c_str(), options);
  if (impl_->session.GetInputCount() != 1) {
    throw std::runtime_error("The current native hand pipeline expects exactly one model input.");
  }
  impl_->input_contract = tensor_contract(impl_->session, 0, true);
  element_count(impl_->input_contract.shape);
  impl_->input_names.push_back(impl_->input_contract.name.c_str());
  for (std::size_t index = 0; index < impl_->session.GetOutputCount(); ++index) {
    impl_->output_contracts.push_back(tensor_contract(impl_->session, index, false));
  }
  for (const auto& output : impl_->output_contracts) {
    impl_->output_names.push_back(output.name.c_str());
  }
}

OnnxModel::~OnnxModel() = default;

const TensorContract& OnnxModel::input() const { return impl_->input_contract; }

const std::vector<TensorContract>& OnnxModel::outputs() const { return impl_->output_contracts; }

std::vector<TensorOutput> OnnxModel::run(const std::vector<float>& input_values) const {
  const auto expected_count = element_count(impl_->input_contract.shape);
  if (input_values.size() != expected_count) {
    throw std::runtime_error("Input tensor size does not match the model's declared tensor shape.");
  }
  const auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
  const auto input = Ort::Value::CreateTensor<float>(
      memory, const_cast<float*>(input_values.data()), input_values.size(),
      impl_->input_contract.shape.data(), impl_->input_contract.shape.size());
  const auto raw_outputs =
      impl_->session.Run(Ort::RunOptions{nullptr}, impl_->input_names.data(), &input, 1,
                         impl_->output_names.data(), impl_->output_names.size());
  std::vector<TensorOutput> outputs;
  outputs.reserve(raw_outputs.size());
  for (const auto& raw_output : raw_outputs) {
    const auto info = raw_output.GetTensorTypeAndShapeInfo();
    if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
      throw std::runtime_error("Hand model returned a non-float output tensor.");
    }
    const auto count = info.GetElementCount();
    const auto* data = raw_output.GetTensorData<float>();
    outputs.push_back({info.GetShape(), {data, data + count}});
  }
  return outputs;
}

class OnnxProvider final : public Provider {
 public:
  explicit OnnxProvider(std::string id) : id_(std::move(id)) {}
  ProviderInfo info() const override {
    const auto providers = Ort::GetAvailableProviders();
    const std::string name = id_ == "coreml" ? "CoreMLExecutionProvider" : "CPUExecutionProvider";
    return {id_, name, std::ranges::find(providers, name) != providers.end()};
  }
  std::unique_ptr<Model> load(const std::filesystem::path& path) const override {
    return std::make_unique<OnnxModel>(path, id_);
  }

 private:
  std::string id_;
};
Registry default_registry() {
  Registry registry;
  registry.add("cpu", [] { return std::make_unique<OnnxProvider>("cpu"); });
  registry.add("coreml", [] { return std::make_unique<OnnxProvider>("coreml"); });
  return registry;
}
}  // namespace dualview::inference

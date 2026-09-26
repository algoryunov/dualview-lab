#include <cassert>
#include <cmath>
#include <iostream>

#include "dualview/inference.hpp"
int main(int argc, char** argv) {
  if (argc != 3) return 2;
  auto registry = dualview::inference::default_registry();
  dualview::inference::Controller controller(registry, argv[1], false);
  auto model =
      controller.load(std::filesystem::path(argv[2]) / "palm_detection_mediapipe_2023feb.onnx");
  std::size_t count = 1;
  for (auto d : model->input().shape) count *= d;
  auto output = model->run(std::vector<float>(count, 0));
  assert(output.size() == 2);
  assert(output[0].values.size() == 2016 * 18);
  assert(output[1].values.size() == 2016);
  for (const auto& tensor : output)
    for (auto value : tensor.values) assert(std::isfinite(value));
  bool rejected = false;
  try {
    model->run({0});
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  assert(rejected);
  auto hand = controller.load(std::filesystem::path(argv[2]) /
                              "handpose_estimation_mediapipe_2023feb.onnx");
  count = 1;
  for (auto d : hand->input().shape) count *= d;
  auto landmarks = hand->run(std::vector<float>(count, 0));
  assert(landmarks.size() == 4);
  assert(landmarks[0].values.size() == 63);
  std::cout << "Model tensors verified on " << controller.info().device << '\n';
}

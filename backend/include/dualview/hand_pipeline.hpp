#pragma once
#include <array>
#include <opencv2/core.hpp>
#include <optional>

#include "dualview/inference.hpp"
namespace dualview {
struct Hand {
  std::array<cv::Point2f, 21> landmarks;
  float confidence;
  bool right;
};
class HandPipeline {
 public:
  HandPipeline(const inference::Controller&, const std::filesystem::path& models);
  ~HandPipeline();
  std::vector<Hand> detect(const cv::Mat& frame) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace dualview

#pragma once
#include <array>
#include <opencv2/core.hpp>
#include <optional>

namespace dualview {
struct HandCrop {
  cv::Mat image;  // Square BGR crop; resize to 224 before RGB tensor conversion.
  cv::Matx23d model_to_image;
  cv::Point2f project(cv::Point2f model_point) const;
};
// OpenCV Zoo MediaPipe hand-pose preprocessing: padded rotation, landmark-based
// palm bounds, finger-directed shift, final square padding, inverse transform.
std::optional<HandCrop> prepare_hand_crop(const cv::Mat& image, const cv::Rect2f& palm_box,
                                          const std::array<cv::Point2f, 7>& landmarks);
}  // namespace dualview

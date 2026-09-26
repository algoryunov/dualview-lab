#include "dualview/hand_pipeline.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <stdexcept>
#include <string>
#include <vector>

#include "dualview/hand_crop.hpp"

namespace dualview {

constexpr int kPalmSize = 192;
constexpr int kHandSize = 224;
constexpr float kPalmThreshold = 0.55F;
constexpr float kHandThreshold = 0.55F;
constexpr float kNmsThreshold = 0.30F;

struct Palm {
  cv::Rect2f box;
  std::array<cv::Point2f, 7> landmarks;
  float score;
};

float sigmoid(const float value) { return 1.0F / (1.0F + std::exp(-value)); }

float intersection_over_union(const cv::Rect2f& first, const cv::Rect2f& second) {
  const auto intersection = first & second;
  const float intersection_area = intersection.area();
  const float union_area = first.area() + second.area() - intersection_area;
  return union_area > 0.0F ? intersection_area / union_area : 0.0F;
}

std::vector<cv::Point2f> palm_anchors() {
  std::vector<cv::Point2f> anchors;
  anchors.reserve(2016);
  for (int row = 0; row < 24; ++row) {
    for (int column = 0; column < 24; ++column) {
      const cv::Point2f anchor{(static_cast<float>(column) + 0.5F) / 24.0F,
                               (static_cast<float>(row) + 0.5F) / 24.0F};
      anchors.push_back(anchor);
      anchors.push_back(anchor);
    }
  }
  for (int row = 0; row < 12; ++row) {
    for (int column = 0; column < 12; ++column) {
      const cv::Point2f anchor{(static_cast<float>(column) + 0.5F) / 12.0F,
                               (static_cast<float>(row) + 0.5F) / 12.0F};
      for (int duplicate = 0; duplicate < 6; ++duplicate) {
        anchors.push_back(anchor);
      }
    }
  }
  if (anchors.size() != 2016) {
    throw std::runtime_error("Palm anchor generation produced an unexpected count.");
  }
  return anchors;
}

std::vector<float> nhwc_rgb_tensor(const cv::Mat& bgr, const int side) {
  if (bgr.empty()) {
    throw std::runtime_error("Cannot preprocess an empty frame.");
  }
  cv::Mat rgb;
  cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
  cv::Mat resized;
  cv::resize(rgb, resized, {side, side}, 0.0, 0.0, cv::INTER_AREA);
  std::vector<float> tensor(static_cast<std::size_t>(side * side * 3));
  for (int row = 0; row < side; ++row) {
    for (int column = 0; column < side; ++column) {
      const auto pixel = resized.at<cv::Vec3b>(row, column);
      const auto offset = static_cast<std::size_t>((row * side + column) * 3);
      tensor[offset] = static_cast<float>(pixel[0]) / 255.0F;
      tensor[offset + 1] = static_cast<float>(pixel[1]) / 255.0F;
      tensor[offset + 2] = static_cast<float>(pixel[2]) / 255.0F;
    }
  }
  return tensor;
}

struct PalmInputTransform {
  float ratio;
  float pad_x;
  float pad_y;
};

std::pair<std::vector<float>, PalmInputTransform> palm_tensor(const cv::Mat& bgr) {
  const float ratio =
      std::min(static_cast<float>(kPalmSize) / bgr.cols, static_cast<float>(kPalmSize) / bgr.rows);
  const cv::Size scaled{std::max(1, static_cast<int>(bgr.cols * ratio)),
                        std::max(1, static_cast<int>(bgr.rows * ratio))};
  cv::Mat resized;
  cv::resize(bgr, resized, scaled, 0.0, 0.0, cv::INTER_AREA);
  const int left = (kPalmSize - scaled.width) / 2;
  const int top = (kPalmSize - scaled.height) / 2;
  cv::Mat padded = cv::Mat::zeros(kPalmSize, kPalmSize, CV_8UC3);
  resized.copyTo(padded(cv::Rect{left, top, scaled.width, scaled.height}));
  return {nhwc_rgb_tensor(padded, kPalmSize),
          {ratio, static_cast<float>(left) / ratio, static_cast<float>(top) / ratio}};
}

struct HandPipeline::Impl {
 public:
  Impl(const inference::Controller& controller, const std::filesystem::path& palm_model,
       const std::filesystem::path& hand_model)
      : palm_model_(controller.load(palm_model)),
        hand_model_(controller.load(hand_model)),
        anchors_(palm_anchors()) {}

  std::vector<Hand> detect(const cv::Mat& bgr) const {
    const auto palms = detect_palms(bgr);
    std::vector<Hand> hands;
    hands.reserve(palms.size());
    for (const auto& palm : palms) {
      if (const auto hand = detect_hand(bgr, palm)) {
        hands.push_back(*hand);
      }
    }
    return hands;
  }

 private:
  std::vector<Palm> detect_palms(const cv::Mat& bgr) const {
    const auto [input, transform] = palm_tensor(bgr);
    const auto outputs = palm_model_->run(input);
    if (outputs.size() != 2 || outputs[0].values.size() != 2016 * 18 ||
        outputs[1].values.size() != 2016) {
      throw std::runtime_error("Palm model output contract changed.");
    }
    std::vector<Palm> candidates;
    candidates.reserve(32);
    const float scale = static_cast<float>(std::max(bgr.cols, bgr.rows));
    for (std::size_t index = 0; index < anchors_.size(); ++index) {
      const float score = sigmoid(outputs[1].values[index]);
      if (score < kPalmThreshold) {
        continue;
      }
      const auto* delta = outputs[0].values.data() + index * 18;
      const float center_x = (delta[0] / kPalmSize + anchors_[index].x) * scale - transform.pad_x;
      const float center_y = (delta[1] / kPalmSize + anchors_[index].y) * scale - transform.pad_y;
      const float width = delta[2] / kPalmSize * scale;
      const float height = delta[3] / kPalmSize * scale;
      Palm palm{{center_x - width / 2.0F, center_y - height / 2.0F, width, height}, {}, score};
      for (int landmark = 0; landmark < 7; ++landmark) {
        palm.landmarks[landmark] = {
            (delta[4 + landmark * 2] / kPalmSize + anchors_[index].x) * scale - transform.pad_x,
            (delta[5 + landmark * 2] / kPalmSize + anchors_[index].y) * scale - transform.pad_y,
        };
      }
      candidates.push_back(palm);
    }
    std::ranges::sort(candidates, {}, &Palm::score);
    std::reverse(candidates.begin(), candidates.end());
    std::vector<Palm> selected;
    selected.reserve(2);
    for (const auto& candidate : candidates) {
      const bool overlaps = std::ranges::any_of(selected, [&](const auto& kept) {
        return intersection_over_union(candidate.box, kept.box) > kNmsThreshold;
      });
      if (!overlaps) {
        selected.push_back(candidate);
      }
      if (selected.size() == 2) {
        break;
      }
    }
    return selected;
  }

  std::optional<Hand> detect_hand(const cv::Mat& bgr, const Palm& palm) const {
    const auto crop = prepare_hand_crop(bgr, palm.box, palm.landmarks);
    if (!crop) return std::nullopt;
    const auto outputs = hand_model_->run(nhwc_rgb_tensor(crop->image, kHandSize));
    if (outputs.size() != 4 || outputs[0].values.size() != 63 || outputs[1].values.size() != 1 ||
        outputs[2].values.size() != 1) {
      throw std::runtime_error("Hand landmark model output contract changed.");
    }
    const float confidence = outputs[1].values[0];
    if (!std::isfinite(confidence) || confidence < kHandThreshold) {
      return std::nullopt;
    }
    Hand hand{{}, confidence, outputs[2].values[0] >= 0.5F};
    for (int index = 0; index < 21; ++index) {
      hand.landmarks[index] =
          crop->project({outputs[0].values[index * 3], outputs[0].values[index * 3 + 1]});
      if (!std::isfinite(hand.landmarks[index].x) || !std::isfinite(hand.landmarks[index].y))
        return std::nullopt;
    }
    return hand;
  }

  std::unique_ptr<inference::Model> palm_model_;
  std::unique_ptr<inference::Model> hand_model_;
  std::vector<cv::Point2f> anchors_;
};

HandPipeline::HandPipeline(const inference::Controller& controller,
                           const std::filesystem::path& models)
    : impl_(std::make_unique<Impl>(controller, models / "palm_detection_mediapipe_2023feb.onnx",
                                   models / "handpose_estimation_mediapipe_2023feb.onnx")) {}
HandPipeline::~HandPipeline() = default;
std::vector<Hand> HandPipeline::detect(const cv::Mat& frame) const { return impl_->detect(frame); }
}  // namespace dualview

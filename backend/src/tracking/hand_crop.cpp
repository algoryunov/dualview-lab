#include "dualview/hand_crop.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <opencv2/imgproc.hpp>

namespace dualview {
namespace {
struct PaddedCrop {
  cv::Mat image;
  cv::Rect bounds;
  cv::Point2d bias;
};
std::optional<PaddedCrop> crop_and_pad(const cv::Mat& image, const cv::Rect2d& box,
                                       bool for_rotation) {
  if (!std::isfinite(box.x) || !std::isfinite(box.y) || !std::isfinite(box.width) ||
      !std::isfinite(box.height) || box.width <= 0 || box.height <= 0)
    return {};
  const double factor = for_rotation ? 4 : 3;
  const cv::Point2d center(box.x + box.width / 2, box.y + box.height * (for_rotation ? .5 : .1));
  // Clamp before integer conversion; malformed model outputs cannot overflow.
  const int left =
      static_cast<int>(std::clamp(center.x - factor * box.width / 2, 0., double(image.cols)));
  const int top =
      static_cast<int>(std::clamp(center.y - factor * box.height / 2, 0., double(image.rows)));
  const int right =
      static_cast<int>(std::clamp(center.x + factor * box.width / 2, 0., double(image.cols)));
  const int bottom =
      static_cast<int>(std::clamp(center.y + factor * box.height / 2, 0., double(image.rows)));
  const cv::Rect bounds(left, top, right - left, bottom - top);
  if (bounds.width <= 0 || bounds.height <= 0) return {};
  const int side = for_rotation ? int(std::hypot(bounds.width, bounds.height))
                                : std::max(bounds.width, bounds.height);
  const int pad_x = (side - bounds.width) / 2, pad_y = (side - bounds.height) / 2;
  cv::Mat padded;
  cv::copyMakeBorder(image(bounds), padded, pad_y, side - bounds.height - pad_y, pad_x,
                     side - bounds.width - pad_x, cv::BORDER_CONSTANT | cv::BORDER_ISOLATED,
                     cv::Scalar{});
  return PaddedCrop{padded, bounds, {double(left - pad_x), double(top - pad_y)}};
}
cv::Point2d transform(const cv::Mat& matrix, cv::Point2d p) {
  return {matrix.at<double>(0, 0) * p.x + matrix.at<double>(0, 1) * p.y + matrix.at<double>(0, 2),
          matrix.at<double>(1, 0) * p.x + matrix.at<double>(1, 1) * p.y + matrix.at<double>(1, 2)};
}
}  // namespace
cv::Point2f HandCrop::project(cv::Point2f p) const {
  const auto result = model_to_image * cv::Vec3d(p.x, p.y, 1);
  return {float(result[0]), float(result[1])};
}
std::optional<HandCrop> prepare_hand_crop(const cv::Mat& image, const cv::Rect2f& box,
                                          const std::array<cv::Point2f, 7>& landmarks) {
  if (image.empty() || image.type() != CV_8UC3) return {};
  for (const auto& p : landmarks)
    if (!std::isfinite(p.x) || !std::isfinite(p.y)) return {};
  const auto first = crop_and_pad(image, box, true);
  if (!first) return {};
  const auto direction = cv::Point2d(landmarks[2]) - cv::Point2d(landmarks[0]);
  if (cv::norm(direction) < 1e-5) return {};
  const double angle = std::remainder(
      90. - std::atan2(-double(direction.y), double(direction.x)) * 180. / std::numbers::pi, 360.);
  const cv::Point2d center(first->bounds.x + first->bounds.width / 2. - first->bias.x,
                           first->bounds.y + first->bounds.height / 2. - first->bias.y);
  const auto rotation = cv::getRotationMatrix2D(center, angle, 1);
  cv::Mat rotated;
  cv::warpAffine(first->image, rotated, rotation, first->image.size(), cv::INTER_LINEAR,
                 cv::BORDER_CONSTANT);
  cv::Point2d low(INFINITY, INFINITY), high(-INFINITY, -INFINITY);
  for (const auto& landmark : landmarks) {
    const auto p = transform(rotation, cv::Point2d(landmark) - first->bias);
    low.x = std::min(low.x, p.x);
    low.y = std::min(low.y, p.y);
    high.x = std::max(high.x, p.x);
    high.y = std::max(high.y, p.y);
  }
  const auto final = crop_and_pad(rotated, {low.x, low.y, high.x - low.x, high.y - low.y}, false);
  if (!final) return {};
  cv::Mat inverse;
  cv::invertAffineTransform(rotation, inverse);
  const double scale = std::max(final->bounds.width, final->bounds.height) / 224.;
  const cv::Point2d offset(final->bounds.x + final->bounds.width / 2. - 112 * scale,
                           final->bounds.y + final->bounds.height / 2. - 112 * scale);
  const auto origin = transform(inverse, offset) + first->bias;
  return HandCrop{final->image,
                  {inverse.at<double>(0, 0) * scale, inverse.at<double>(0, 1) * scale, origin.x,
                   inverse.at<double>(1, 0) * scale, inverse.at<double>(1, 1) * scale, origin.y}};
}
}  // namespace dualview

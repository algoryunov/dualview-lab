#include "dualview/hand_crop.hpp"

#include <cassert>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <opencv2/imgproc.hpp>

int main() {
  using nlohmann::json;
  std::ifstream input(DUALVIEW_CROP_FIXTURE);
  assert(input.good());
  const auto fixture = json::parse(input);
  cv::Mat image(240, 320, CV_8UC3);
  for (int y = 0; y < image.rows; ++y)
    for (int x = 0; x < image.cols; ++x)
      image.at<cv::Vec3b>(y, x) = {uchar(x % 251), uchar(y % 241), uchar((x + y) % 239)};
  for (const auto& sample : fixture["cases"]) {
    const auto& b = sample["box"];
    const cv::Rect2f box(b[0], b[1], b[2], b[3]);
    std::array<cv::Point2f, 7> points;
    for (int i = 0; i < 7; ++i) points[i] = {sample["landmarks"][i][0], sample["landmarks"][i][1]};
    const auto crop = dualview::prepare_hand_crop(image, box, points);
    assert(crop && crop->image.cols == crop->image.rows);
    for (int i = 0; i < 21; ++i) {
      const auto p = crop->project({sample["model_points"][i][0], sample["model_points"][i][1]});
      const cv::Point2f expected(sample["image_points"][i][0], sample["image_points"][i][1]);
      if (cv::norm(p - expected) > .02) {
        std::cerr << sample["name"] << " coordinate mismatch " << p << " vs " << expected << '\n';
        return 1;
      }
    }
    cv::Mat rgb, resized;
    cv::cvtColor(crop->image, rgb, cv::COLOR_BGR2RGB);
    cv::resize(rgb, resized, {224, 224}, 0, 0, cv::INTER_AREA);
    for (const auto& probe : sample["rgb_probes"]) {
      const auto pixel = resized.at<cv::Vec3b>(probe[1].get<int>(), probe[0].get<int>());
      for (int c = 0; c < 3; ++c) {
        // Permit small interpolation differences across OpenCV versions.
        if (std::abs(pixel[c] / 255. - probe[c + 2].get<double>()) > .025) {
          std::cerr << sample["name"] << " RGB crop mismatch at " << probe[0] << ',' << probe[1]
                    << " channel " << c << " actual " << pixel[c] / 255. << " expected "
                    << probe[c + 2] << '\n';
          return 1;
        }
      }
    }
    assert(!dualview::prepare_hand_crop({}, box, points));
    assert(!dualview::prepare_hand_crop(image, {0, 0, 0, 1}, points));
    points[0].x = std::numeric_limits<float>::quiet_NaN();
    assert(!dualview::prepare_hand_crop(image, box, points));
  }
  std::cout << "Crop pixels and inverse landmarks match OpenCV Zoo reference in six poses\n";
}

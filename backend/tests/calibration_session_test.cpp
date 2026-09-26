#include "dualview/calibration_session.hpp"

#include <cassert>
using namespace dualview;
int main() {
  CalibrationSession session;
  CameraIntrinsics intrinsics[2];
  StereoSolution stereo;
  CameraFrame frames[2];
  frames[1] = {cv::Mat::zeros(480, 640, CV_8UC3), Clock::now(), 1, {}, {}};
  assert(!session.active() && !session.process(frames, intrinsics, stereo, 0));
  session.start(CalibrationMode::Phone, frames[1].image.size());
  assert(session.active() && session.uses_phone() && !session.stereo());
  assert(session.progress["required_views"] == 25 && session.progress["camera_id"] == "phone");
  bool rejected = false;
  try {
    session.start(CalibrationMode::Stereo);
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  assert(rejected);
  assert(!session.process(frames, intrinsics, stereo, 0));
  assert(session.progress["rejected_views"] == 1);
  session.cancel();
  assert(!session.active() && !session.process(frames, intrinsics, stereo, 0));
  session.start(CalibrationMode::Stereo);
  assert(session.progress["required_views"] == 20 && session.progress["rejected_views"] == 0);
  session.cancel();
  session.start(CalibrationMode::Laptop, {320, 240});
  assert(!session.uses_phone());
  frames[0] = frames[1];
  rejected = false;
  try {
    session.process(frames, intrinsics, stereo, 0);
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  assert(rejected);
}

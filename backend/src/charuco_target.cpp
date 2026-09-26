#include <iostream>
#include <opencv2/imgcodecs.hpp>

#include "dualview/calibration.hpp"
int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "Usage: dualview_charuco OUTPUT.png\n";
    return 2;
  }
  try {
    // A4 at 300 DPI. The board is 175 x 245 mm with a 35 mm square.
    cv::Mat page(3508, 2480, CV_8UC1, cv::Scalar(255)), board;
    dualview::make_charuco_board({}).generateImage({2067, 2894}, board, 0, 1);
    board.copyTo(page(cv::Rect((page.cols - board.cols) / 2, (page.rows - board.rows) / 2,
                               board.cols, board.rows)));
    if (!cv::imwrite(argv[1], page)) throw std::runtime_error("Could not write target");
    std::cout << "Print on A4 at actual size; verify each square measures 35 mm.\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

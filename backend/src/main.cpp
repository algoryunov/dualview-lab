#include <gst/gst.h>

#include <iostream>

#include "dualview/transport.hpp"
int main(int argc, char** argv) {
  try {
    gst_init(&argc, &argv);
    dualview::Config config;
    dualview::Runtime runtime(config);
    return dualview::serve(config, runtime);
  } catch (const std::exception& error) {
    std::cerr << "Startup failed: " << error.what() << '\n';
    return 1;
  }
}

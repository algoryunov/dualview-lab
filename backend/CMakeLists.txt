cmake_minimum_required(VERSION 3.25)
project(dualview LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_NO_SYSTEM_FROM_IMPORTED ON)
option(DUALVIEW_TSAN "Instrument native code with ThreadSanitizer" OFF)
if(DUALVIEW_TSAN)
  add_compile_options(-fsanitize=thread -g)
  add_link_options(-fsanitize=thread)
endif()
option(DUALVIEW_BUILD_SERVER "Build the HTTPS/WebRTC application" ON)

find_package(OpenCV 4.8 REQUIRED COMPONENTS core imgproc imgcodecs videoio calib3d aruco)
find_package(nlohmann_json 3.11 REQUIRED)
find_package(Threads REQUIRED)
add_library(dualview_core INTERFACE)
target_include_directories(dualview_core INTERFACE include)
target_link_libraries(dualview_core INTERFACE Threads::Threads)
set(DUALVIEW_ONNXRUNTIME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/deps/onnxruntime-1.30.0" CACHE PATH "Explicit ONNX Runtime installation")
find_path(ORT_INCLUDE onnxruntime_cxx_api.h PATHS "${DUALVIEW_ONNXRUNTIME_ROOT}/include" REQUIRED NO_DEFAULT_PATH)
find_library(ORT_LIBRARY onnxruntime PATHS "${DUALVIEW_ONNXRUNTIME_ROOT}/lib" REQUIRED NO_DEFAULT_PATH)
add_library(dualview_ort SHARED IMPORTED)
set_target_properties(dualview_ort PROPERTIES IMPORTED_LOCATION "${ORT_LIBRARY}" INTERFACE_INCLUDE_DIRECTORIES "${ORT_INCLUDE}")

function(dualview_target target)
  target_include_directories(${target} PUBLIC include)
  target_compile_options(${target} PRIVATE
    $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>
    $<$<CXX_COMPILER_ID:AppleClang,Clang>:-Wno-c11-extensions>)
endfunction()

add_library(dualview_geometry src/calibration.cpp)
dualview_target(dualview_geometry)
target_include_directories(dualview_geometry BEFORE PUBLIC ${OpenCV_INCLUDE_DIRS})
# Keep the selected OpenCV headers ahead of stale developer CPATH entries.
foreach(directory IN LISTS OpenCV_INCLUDE_DIRS)
  target_compile_options(dualview_geometry PUBLIC "-I${directory}")
endforeach()
target_link_libraries(dualview_geometry PUBLIC opencv_core opencv_imgproc opencv_calib3d opencv_aruco)

add_library(dualview_inference src/inference/controller.cpp src/inference/providers/onnx.cpp)
dualview_target(dualview_inference)
target_link_libraries(dualview_inference PRIVATE dualview_ort)

add_library(dualview_runtime
  src/config.cpp
  src/runtime.cpp
  src/runtime_protocol.cpp
  src/diagnostic_session.cpp
  src/calibration_session.cpp
  src/calibration_storage.cpp
  src/tracking/interaction.cpp
  src/tracking/hand_crop.cpp
  src/tracking/hand_pipeline.cpp
  src/tracking/stereo_tracking.cpp)
dualview_target(dualview_runtime)
target_link_libraries(dualview_runtime PUBLIC dualview_geometry dualview_inference nlohmann_json::nlohmann_json PRIVATE opencv_videoio)

if(DUALVIEW_BUILD_SERVER)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GST REQUIRED IMPORTED_TARGET
    gstreamer-webrtc-1.0>=1.24 gstreamer-sdp-1.0 gstreamer-app-1.0
    gstreamer-video-1.0 libsoup-3.0>=3.2)
  add_library(dualview_transport src/transport/server.cpp src/transport/peer.cpp)
  dualview_target(dualview_transport)
  target_link_libraries(dualview_transport PUBLIC dualview_runtime PRIVATE PkgConfig::GST)
  add_executable(dualview_server src/main.cpp)
  target_link_libraries(dualview_server PRIVATE dualview_transport PkgConfig::GST)
endif()

add_executable(dualview_charuco src/charuco_target.cpp)
target_link_libraries(dualview_charuco PRIVATE dualview_geometry opencv_imgcodecs)

add_executable(geometry_evaluation benchmarks/geometry_evaluation.cpp)
target_link_libraries(geometry_evaluation PRIVATE dualview_runtime)
target_compile_definitions(geometry_evaluation PRIVATE DUALVIEW_BUILD_TYPE="${CMAKE_BUILD_TYPE}")

include(CTest)
if(BUILD_TESTING)
  add_test(NAME geometry_evaluation COMMAND geometry_evaluation --check)
  function(dualview_test name library)
    add_executable(${name}_test tests/${name}_test.cpp)
    target_link_libraries(${name}_test PRIVATE ${library})
    target_compile_options(${name}_test PRIVATE $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-UNDEBUG>)
    add_test(NAME ${name} COMMAND ${name}_test)
  endfunction()
  dualview_test(calibration_geometry dualview_geometry)
  dualview_test(inference dualview_inference)
  dualview_test(runtime dualview_runtime)
  dualview_test(runtime_concurrency dualview_runtime)
  dualview_test(work_revisions dualview_core)
  dualview_test(diagnostic_session dualview_runtime)
  dualview_test(synchronized_frames dualview_runtime)
  dualview_test(motion_timing dualview_runtime)
  dualview_test(clock_sync dualview_runtime)
  dualview_test(tracking dualview_runtime)
  dualview_test(interaction dualview_runtime)
  dualview_test(calibration_session dualview_runtime)
  dualview_test(data_outbox dualview_core)
  dualview_test(hand_crop dualview_runtime)
  target_compile_definitions(hand_crop_test PRIVATE
    DUALVIEW_CROP_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/hand_crop_reference.json")
  dualview_test(config dualview_runtime)
  dualview_test(event_log dualview_runtime)
  add_executable(model_inference_test tests/model_inference_test.cpp)
  target_link_libraries(model_inference_test PRIVATE dualview_inference)
  target_compile_options(model_inference_test PRIVATE $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-UNDEBUG>)
  set(DUALVIEW_TEST_MODELS "${CMAKE_CURRENT_SOURCE_DIR}/../models/onnx" CACHE PATH "Local inference test models")
  if(EXISTS "${DUALVIEW_TEST_MODELS}/palm_detection_mediapipe_2023feb.onnx" AND
     EXISTS "${DUALVIEW_TEST_MODELS}/handpose_estimation_mediapipe_2023feb.onnx")
    add_test(NAME model_cpu COMMAND model_inference_test cpu "${DUALVIEW_TEST_MODELS}")
    if(APPLE)
      add_test(NAME model_coreml COMMAND model_inference_test coreml "${DUALVIEW_TEST_MODELS}")
    endif()
  endif()
endif()

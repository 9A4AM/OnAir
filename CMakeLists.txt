cmake_minimum_required(VERSION 3.20)
project(DecT2 LANGUAGES C CXX OBJCXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 20)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3")
set(CMAKE_OBJCXX_FLAGS_RELEASE "-O3")

find_package(PkgConfig REQUIRED)
pkg_check_modules(HACKRF REQUIRED IMPORTED_TARGET libhackrf)
pkg_check_modules(GLFW REQUIRED IMPORTED_TARGET glfw3)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale libswresample)

add_library(dect2core STATIC core/src/source.cpp core/src/spectrum.cpp core/src/engine.cpp core/src/t2_tables.cpp core/src/t2gen.cpp core/src/t2rx.cpp core/src/t2pilots.cpp core/src/t2ofdm.cpp core/src/ldpc.cpp core/src/t2l1.cpp core/src/t2interleave.cpp core/src/t2fec_tables.cpp core/src/t2fec.cpp core/src/t2plp.cpp core/src/resampler.cpp core/src/bbunpack.cpp core/src/ts.cpp core/src/tsout.cpp core/src/scanner.cpp core/src/gain.cpp core/src/channel.cpp core/src/gpu_ldpc.mm core/src/player.cpp core/src/audioout.mm core/src/t2_pilot_tables.cpp)
target_include_directories(dect2core PUBLIC core/include)
target_compile_options(dect2core PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fcx-limited-range>)
set_source_files_properties(core/src/gpu_ldpc.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
target_link_libraries(dect2core PUBLIC PkgConfig::HACKRF PkgConfig::FFMPEG "-framework Accelerate" "-framework AudioToolbox" "-framework CoreAudio" "-framework CoreFoundation" "-framework Metal" "-framework Foundation" "-framework VideoToolbox" "-framework CoreMedia" "-framework CoreVideo" pthread iconv)

add_library(imgui STATIC
  third_party/imgui/imgui.cpp third_party/imgui/imgui_draw.cpp third_party/imgui/imgui_tables.cpp third_party/imgui/imgui_widgets.cpp
  third_party/imgui/backends/imgui_impl_glfw.cpp third_party/imgui/backends/imgui_impl_metal.mm
  third_party/implot/implot.cpp third_party/implot/implot_items.cpp)
target_include_directories(imgui PUBLIC third_party/imgui third_party/imgui/backends third_party/implot)
target_link_libraries(imgui PUBLIC PkgConfig::GLFW "-framework Metal" "-framework MetalKit" "-framework QuartzCore" "-framework Cocoa" "-framework IOKit" "-framework CoreVideo")
target_compile_options(imgui PRIVATE -fobjc-arc)

add_executable(dect2 app/main.mm)
target_compile_options(dect2 PRIVATE -fobjc-arc)
target_link_libraries(dect2 PRIVATE dect2core imgui)

add_executable(test_sync tests/test_sync.cpp)
target_link_libraries(test_sync PRIVATE dect2core)
enable_testing()
add_test(NAME sync COMMAND test_sync)

add_executable(test_l1 tests/test_l1.cpp)
target_link_libraries(test_l1 PRIVATE dect2core)
add_test(NAME l1 COMMAND test_l1)

add_executable(test_pilots tests/test_pilots.cpp)
target_link_libraries(test_pilots PRIVATE dect2core)
add_test(NAME pilots COMMAND test_pilots)

add_executable(dect2cli tools/dect2cli.cpp)
target_link_libraries(dect2cli PRIVATE dect2core)

add_executable(test_fec tests/test_fec.cpp)
target_link_libraries(test_fec PRIVATE dect2core)
add_test(NAME fec COMMAND test_fec)

add_executable(bench_fec tests/bench_fec.cpp)
target_link_libraries(bench_fec PRIVATE dect2core)

add_executable(test_ts tests/test_ts.cpp)
target_link_libraries(test_ts PRIVATE dect2core)
add_test(NAME ts COMMAND test_ts)

add_executable(dect2scan tools/dect2scan.cpp)
target_link_libraries(dect2scan PRIVATE dect2core)

add_executable(test_gpu_ldpc tests/test_gpu_ldpc.cpp)
target_link_libraries(test_gpu_ldpc PRIVATE dect2core)

add_executable(test_gain tests/test_gain.cpp)
target_link_libraries(test_gain PRIVATE dect2core)

add_executable(test_channel tests/test_channel.cpp)
target_link_libraries(test_channel PRIVATE dect2core)

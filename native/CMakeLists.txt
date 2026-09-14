find_package(glm CONFIG REQUIRED)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  set(SODA_WARNINGS -Wall -Wextra -Wpedantic)
endif()

# Dataset access: OFF meshes and the manifest. No Qt, no GPU.
add_library(soda
  mesh/json.cpp
  mesh/off.cpp
  mesh/manifest.cpp
)
target_include_directories(soda PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(soda PUBLIC cxx_std_20)
target_link_libraries(soda PUBLIC glm::glm)
target_compile_options(soda PRIVATE ${SODA_WARNINGS})

add_executable(soda_inspect tools/inspect.cpp)
target_link_libraries(soda_inspect PRIVATE soda)
target_compile_options(soda_inspect PRIVATE ${SODA_WARNINGS})

option(SODA_BUILD_GUI "Build the Qt 6 viewer" OFF)
if(SODA_BUILD_GUI)
  find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets OpenGLWidgets OpenGL)
  add_executable(soda_viewer
    viewer/main.cpp
    viewer/main_window.cpp
    viewer/capture.cpp
    viewer/viewport.cpp
  )
  target_link_libraries(soda_viewer PRIVATE soda Qt6::Widgets Qt6::OpenGLWidgets Qt6::OpenGL)
  target_compile_options(soda_viewer PRIVATE ${SODA_WARNINGS})
endif()

if(BUILD_TESTING)
  function(soda_test name source)
    add_executable(soda_${name}_tests ${source})
    target_link_libraries(soda_${name}_tests PRIVATE soda)
    target_compile_options(soda_${name}_tests PRIVATE ${SODA_WARNINGS})
  endfunction()

  soda_test(off tests/off_tests.cpp)
  add_test(NAME off_import COMMAND soda_off_tests "${CMAKE_CURRENT_BINARY_DIR}/off-test-data")

  soda_test(manifest tests/manifest_tests.cpp)
  add_test(NAME manifest_import COMMAND soda_manifest_tests
    "${CMAKE_CURRENT_BINARY_DIR}/manifest-test-data")

  if(EXISTS "${PROJECT_SOURCE_DIR}/data/manifest.json")
    add_test(NAME dataset_consistency COMMAND soda_inspect
      "${PROJECT_SOURCE_DIR}/data/manifest.json" --verify)
  endif()
endif()

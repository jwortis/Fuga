cmake_minimum_required(VERSION 3.22)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "")
project(Fuga VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# --- Core: score model, fugue rules, generation. No JUCE. ---
add_library(fuga_core STATIC
  core/Theory.cpp
  core/Rules.cpp
  core/Generate.cpp)
target_include_directories(fuga_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(fuga_core_tests tests/CoreTests.cpp)
target_link_libraries(fuga_core_tests PRIVATE fuga_core)
enable_testing()
add_test(NAME core COMMAND fuga_core_tests)

# --- App ---
add_subdirectory(external/JUCE EXCLUDE_FROM_ALL)

juce_add_gui_app(Fuga
  PRODUCT_NAME "Fuga"
  BUNDLE_ID "app.fuga.Fuga"
  COMPANY_NAME "Fuga")

target_sources(Fuga PRIVATE
  app/Main.cpp
  app/Document.cpp
  app/AudioEngine.cpp
  app/TimelineView.cpp
  app/PianoRollView.cpp
  app/MainComponent.cpp)

target_compile_definitions(Fuga PRIVATE
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_PLUGINHOST_VST3=1
  JUCE_PLUGINHOST_AU=1
  JUCE_DISPLAY_SPLASH_SCREEN=0
  JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Fuga,JUCE_PRODUCT_NAME>"
  JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Fuga,JUCE_VERSION>")

target_link_libraries(Fuga PRIVATE
  fuga_core
  juce::juce_audio_utils
  juce::juce_audio_processors
  juce::juce_recommended_config_flags
  juce::juce_recommended_lto_flags)

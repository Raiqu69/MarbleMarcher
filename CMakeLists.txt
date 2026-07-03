cmake_minimum_required(VERSION 3.13)
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
project(MarbleMarcher)
cmake_policy(SET CMP0079 NEW)
set(CMAKE_CXX_STANDARD 17)

## DEPENDENCIES

find_package(Eigen3 REQUIRED)
find_package(SFML 3 COMPONENTS System Window Graphics Audio REQUIRED)

## TARGETS

add_subdirectory(src)

target_include_directories(MarbleMarcherSources PUBLIC
  ${EIGEN3_INCLUDE_DIR}
)
target_link_libraries(MarbleMarcherSources PUBLIC
  Eigen3::Eigen
  SFML::System
  SFML::Window
  SFML::Graphics
  SFML::Audio
)
target_compile_definitions(MarbleMarcherSources PRIVATE SFML_STATIC)

if(WIN32)
  add_executable(MarbleMarcher WIN32 src/Main.cpp src/Resource.rc assets/icon.ico)
  set_source_files_properties(icon.ico Resource.rc PROPERTIES LANGUAGE RC)
else()
  add_executable(MarbleMarcher src/Main.cpp)
endif()
target_compile_definitions(MarbleMarcher PRIVATE SFML_STATIC)
target_link_libraries(MarbleMarcher
  MarbleMarcherSources
  SFML::System
  SFML::Window
  SFML::Graphics
  SFML::Audio
)

add_custom_command(TARGET MarbleMarcher POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
  "${CMAKE_SOURCE_DIR}/assets"
  "$<TARGET_FILE_DIR:MarbleMarcher>/assets")

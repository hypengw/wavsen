cmake_minimum_required(VERSION 3.28)

project(WavsenShader VERSION 0.1.0 LANGUAGES CXX)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

find_program(GLSLANG_VALIDATOR
    NAMES glslangValidator
    DOC "GLSL to SPIR-V compiler used to bake wavsen shaders")
if(NOT GLSLANG_VALIDATOR)
    message(FATAL_ERROR
        "wavsen: glslangValidator not found in PATH; install Vulkan SDK or glslang")
endif()

set(_wavsen_shader_source ${CMAKE_CURRENT_SOURCE_DIR}/nv12_to_rgba.comp)
set(_wavsen_shader_include ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(_wavsen_shader_header ${_wavsen_shader_include}/nv12_to_rgba.spv.h)

add_custom_command(
    OUTPUT ${_wavsen_shader_header}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${_wavsen_shader_include}
    COMMAND ${GLSLANG_VALIDATOR} -V --vn nv12_to_rgba_spv
            -o ${_wavsen_shader_header} ${_wavsen_shader_source}
    DEPENDS ${_wavsen_shader_source}
    COMMENT "wavsen: compiling NV12 to RGBA compute shader"
    VERBATIM
)
add_custom_target(wavsen_shader_nv12_to_rgba_header ALL
    DEPENDS ${_wavsen_shader_header})

add_library(wavsen_shader_nv12_to_rgba INTERFACE)
add_library(WavsenShader::nv12_to_rgba ALIAS wavsen_shader_nv12_to_rgba)
add_dependencies(wavsen_shader_nv12_to_rgba wavsen_shader_nv12_to_rgba_header)
set_target_properties(wavsen_shader_nv12_to_rgba PROPERTIES
    EXPORT_NAME nv12_to_rgba)
target_include_directories(wavsen_shader_nv12_to_rgba INTERFACE
    $<BUILD_INTERFACE:${_wavsen_shader_include}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/wavsen/shaders>)

install(FILES ${_wavsen_shader_header}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/wavsen/shaders)
install(TARGETS wavsen_shader_nv12_to_rgba
    EXPORT WavsenShaderTargets)
install(EXPORT WavsenShaderTargets
    FILE WavsenShaderTargets.cmake
    NAMESPACE WavsenShader::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/WavsenShader)

configure_package_config_file(
    cmake/WavsenShaderConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/WavsenShaderConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/WavsenShader)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/WavsenShaderConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/WavsenShaderConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/WavsenShaderConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/WavsenShader)

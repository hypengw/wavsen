add_library(wavsen_ffi_vma STATIC vma.cpp)
add_library(wavsen::ffi::vma ALIAS wavsen_ffi_vma)

target_compile_features(wavsen_ffi_vma PUBLIC cxx_std_20)
target_compile_options(wavsen_ffi_vma PRIVATE
    $<$<CXX_COMPILER_ID:Clang>:-Wno-nullability-completeness>
    $<$<CXX_COMPILER_ID:Clang>:-Wno-nullability-extension>
    $<$<CXX_COMPILER_ID:Clang>:-Wno-unused-private-field>
    $<$<CXX_COMPILER_ID:Clang>:-Wno-unused-parameter>
    $<$<CXX_COMPILER_ID:Clang>:-Wno-unused-variable>
)
target_sources(wavsen_ffi_vma
    PUBLIC FILE_SET CXX_MODULES FILES
        vma.cppm
)
target_link_libraries(wavsen_ffi_vma
    PUBLIC
        GPUOpen::VulkanMemoryAllocator
        wavsen::ffi::vulkan
)
set_property(TARGET wavsen_ffi_vma PROPERTY POSITION_INDEPENDENT_CODE ON)
set_target_properties(wavsen_ffi_vma PROPERTIES CXX_SCAN_FOR_MODULES ON)

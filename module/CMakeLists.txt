cmake_minimum_required(VERSION 3.28)

add_library(condy_module)
target_sources(condy_module
    PUBLIC FILE_SET CXX_MODULES
    FILES condy.cppm
)
target_compile_features(condy_module PUBLIC cxx_std_20)
target_link_libraries(condy_module PUBLIC condy)

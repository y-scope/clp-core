set(
        STRING_UTILS_HEADER_LIST
        "string_utils.hpp"
)
add_library(
        string_utils
        string_utils.cpp
        ${STRING_UTILS_HEADER_LIST}
)
add_library(clp::string_utils ALIAS string_utils)
target_include_directories(string_utils PUBLIC ../ PRIVATE "${PROJECT_SOURCE_DIR}/submodules")
target_link_libraries(string_utils PRIVATE simdjson)
target_compile_features(string_utils PRIVATE cxx_std_20)

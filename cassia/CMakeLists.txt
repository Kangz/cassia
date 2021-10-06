cmake_minimum_required(VERSION 3.10)
project(Cassia LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD "14")

set(THIRD_PARTY_DIR "${Cassia_SOURCE_DIR}/third_party")

set(DAWN_ABSEIL_DIR "${THIRD_PARTY_DIR}/abseil")
set(DAWN_GLFW_DIR "${THIRD_PARTY_DIR}/glfw")
set(DAWN_GLM_DIR "${THIRD_PARTY_DIR}/glm")
set(DAWN_JINJA2_DIR "${THIRD_PARTY_DIR}/jinja2")
set(DAWN_SPIRV_HEADERS_DIR "${THIRD_PARTY_DIR}/SPIRV-Headers")
set(DAWN_SPIRV_TOOLS_DIR "${THIRD_PARTY_DIR}/SPIRV-Tools")
set(DAWN_TINT_DIR "${THIRD_PARTY_DIR}/tint")

add_subdirectory("${THIRD_PARTY_DIR}/dawn")

add_library(cassia SHARED
    src/Cassia.cpp
)
target_link_libraries(cassia
    dawn_internal_config
    dawncpp
    dawn_proc
    dawn_utils
    glfw
)

cmake_minimum_required(VERSION 3.27)
project(AIPackagingArchitectureNegativeFixture LANGUAGES NONE)

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/architecture_checks.cmake")

add_library(AIPackaging_FakeGui INTERFACE)
add_library(AIPackaging_FakeCore INTERFACE)
target_link_libraries(AIPackaging_FakeCore INTERFACE AIPackaging_FakeGui)

aipackaging_assert_target_dependencies(TARGET AIPackaging_FakeCore)

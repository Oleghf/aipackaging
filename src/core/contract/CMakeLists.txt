set(contract_static_library "AIPackaging_Contract")

add_library(${contract_static_library} STATIC)

target_sources(${contract_static_library} PRIVATE
"events/scenemouseevent.cpp"
"events/scenepaintevent.cpp")

target_include_directories(${contract_static_library} PUBLIC
"${CMAKE_SOURCE_DIR}/src/core/contract/interfaces"
"${CMAKE_SOURCE_DIR}/src/core/contract/events"
)

target_link_libraries(${contract_static_library} PUBLIC
AIPackaging_Math
)

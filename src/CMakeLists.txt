# src/CMakeLists.txt

set(SRC_SRCS main.cpp strategy.cpp)

# Additional sources for main application
add_library(src_lib ${SRC_SRCS})

target_include_directories(src_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Boost REQUIRED COMPONENTS system thread)

find_package(OpenSSL REQUIRED)

# Include directories for WebSocket++ and Boost
include_directories(
    ${Boost_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
)

# Link dependencies for src library
target_link_libraries(src_lib infra_lib oms_lib)

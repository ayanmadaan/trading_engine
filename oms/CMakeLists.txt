# oms/CMakeLists.txt

set(OMS_SRCS okxordersrouting.cpp okxordermanager.cpp bybitordersrouting.cpp bybitordermanager.cpp bybitfills.cpp)

find_package(Boost REQUIRED COMPONENTS system thread)

find_package(OpenSSL REQUIRED)

# Include directories for WebSocket++ and Boost
include_directories(
    ${Boost_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
)

add_library(oms_lib ${OMS_SRCS})

# Include paths for oms headers
add_compile_options(-O3)
target_include_directories(oms_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

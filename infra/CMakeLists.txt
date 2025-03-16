# infra/CMakeLists.txt

set(INFRA_SRCS pinthreads.cpp websocket.cpp okxwebsocket.cpp binancewebsocket.cpp bybitwebsocket.cpp timer.cpp)

find_package(Boost REQUIRED COMPONENTS system thread)

find_package(OpenSSL REQUIRED)

# Include directories for WebSocket++ and Boost
include_directories(
    ${Boost_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
)

add_library(infra_lib ${INFRA_SRCS})

# Include paths for infra headers
target_include_directories(infra_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

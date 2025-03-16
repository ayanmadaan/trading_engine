cmake_minimum_required(VERSION 3.15)
project(TradingSystem)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SIMD Detection and Configuration
include(CheckCXXCompilerFlag)

# Function to check and add SIMD flags
function(add_advanced_simd_flags)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        # Check for AVX2
        check_cxx_compiler_flag("-mavx2" COMPILER_SUPPORTS_AVX2)
        # Check for BMI1
        check_cxx_compiler_flag("-mbmi" COMPILER_SUPPORTS_BMI)
        # Check for BMI2 (includes TZCNT)
        check_cxx_compiler_flag("-mbmi2" COMPILER_SUPPORTS_BMI2)

        if(COMPILER_SUPPORTS_AVX2)
            add_compile_options(-mavx2)
            add_definitions(-DUSE_AVX2)
        endif()

        if(COMPILER_SUPPORTS_BMI)
            add_compile_options(-mbmi)
            add_definitions(-DUSE_BMI)
        endif()

        if(COMPILER_SUPPORTS_BMI2)
            add_compile_options(-mbmi2)
            add_definitions(-DUSE_BMI2)
        endif()

        # Add general optimization flags
        #add_compile_options(-O3)
        add_compile_options(
            -O3                          # Maximum optimization
            -march=native               # Optimize for local machine architecture
            -mtune=native              # Fine-tune for local machine
            #-fomit-frame-pointer       # Remove frame pointer when possible
            #-flto                      # Link-time optimization
            #-fno-stack-protector       # Disable stack protector for performance
            #-ffast-math                # Aggressive math optimizations
            #-funroll-loops             # Unroll loops
            #-fpeel-loops              # Enable loop peeling
            #-ftracer                  # Enable tail duplication in traces
            #-ftree-vectorize          # Explicitly enable vectorization
            #-fopt-info-vec            # Get vectorization info
            #-fno-strict-aliasing      # Enable type-based alias analysis
            -pthread                  # Enable threading support
            #-finline-functions        # Integrate simple functions into their callers
            #-finline-limit=1000       # Increase inlining limit
            #-fno-rtti                 # Disable runtime type information
            #-fsanitize=address
        )

        # Enable fast math for better SIMD performance
        #add_compile_options(-ffast-math)

        # Enable inline assembly if needed
        #add_compile_options(-masm=intel)

    elseif(MSVC)
        # MSVC flags for AVX2 and BMI2
        check_cxx_compiler_flag("/arch:AVX2" COMPILER_SUPPORTS_AVX2)

        if(COMPILER_SUPPORTS_AVX2)
            add_compile_options(/arch:AVX2)
            add_definitions(-DUSE_AVX2)
            # MSVC automatically enables BMI2 with AVX2
            add_definitions(-DUSE_BMI2)
        endif()

        # Enable fast math
        add_compile_options(/fp:fast)
    endif()
endfunction()

# Call the advanced SIMD configuration function
add_advanced_simd_flags()


set(ROOT_DIR ${CMAKE_SOURCE_DIR})
set(LIB_DIR ${ROOT_DIR}/lib)

# Add directories for each module
add_subdirectory(infra)
add_subdirectory(oms)
add_subdirectory(src)

find_package(Boost REQUIRED COMPONENTS system thread)
find_package(RapidJSON CONFIG REQUIRED)
find_package(ryml CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

# Include directories for WebSocket++ and Boost
include_directories(
    ${Boost_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
)

# Include directories
include_directories(
    ${ROOT_DIR}/infra
    ${ROOT_DIR}/oms
    ${ROOT_DIR}/lib
    ${ROOT_DIR}/src
)

# Define DEBUG macro only for Debug builds
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-DDEBUG_MODE)
endif()

# Link libraries
add_executable(TradingSystem src/main.cpp)

# Add this before target_link_libraries
target_compile_definitions(TradingSystem PRIVATE RYML_NO_DEFAULT_CALLBACKS)

# Link each module's library (defined in their own CMakeLists)
target_link_libraries(TradingSystem
    infra_lib
    oms_lib
    ${Boost_LIBRARIES}
    OpenSSL::SSL
    OpenSSL::Crypto
    ryml::ryml
    CURL::libcurl
)

# Link external JSON library directly
target_include_directories(TradingSystem
                            PRIVATE
                            ${LIB_DIR}
                            ${CURL_INCLUDE_DIRS})

# Optional: Add optimization flags for release builds
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

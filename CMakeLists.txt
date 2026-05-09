# 1. Minimum CMake Version
cmake_minimum_required(VERSION 3.20)

# 2. Project Name and Language
project(LimitOrderBook CXX)

# 3. Language Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# 4. THE GOD MODE OPTIMIZATIONS
# (It's best practice to define these before creating the executable)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -flto -DNDEBUG")
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

# 5. Include Directories
include_directories(include)

# 6. Executable Definition
add_executable(MotorHFT 
    src/main.cpp
    src/OrderBook.cpp
)

# 7. REQUIRED FOR HFT: Link the hardware threading libraries
find_package(Threads REQUIRED)
target_link_libraries(MotorHFT Threads::Threads)
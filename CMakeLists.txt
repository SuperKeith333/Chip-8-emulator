cmake_minimum_required(VERSION 3.15)
project(chip8_emulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_PREFIX_PATH "C:/Users/Keith/vcpkg/installed/x64-windows/share")

# Find SDL2 via vcpkg
find_package(SDL2 CONFIG REQUIRED)

add_executable(chip8_emulator
    src/main.cpp
)

# Link only SDL2, not SDL2main
target_link_libraries(chip8_emulator PRIVATE SDL2::SDL2)


# Include SDL2 headers automatically
target_include_directories(chip8_emulator PRIVATE ${SDL2_INCLUDE_DIRS})

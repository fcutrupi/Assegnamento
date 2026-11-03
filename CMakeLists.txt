cmake_minimum_required(VERSION 3.16)
project(fas_cell_simulator VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# La simulazione usa std::thread, mutex e condition_variable.
find_package(Threads REQUIRED)

# Un unico eseguibile composto da punto di ingresso e moduli del dominio.
add_executable(fas_simulator
    src/main.cpp
    src/Simulation.cpp
    src/Infrastructure.cpp
    src/Domain.cpp
)

# Le intestazioni pubbliche del progetto si trovano nella cartella include/.
target_include_directories(fas_simulator PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(fas_simulator PRIVATE Threads::Threads)

if(MSVC)
    target_compile_options(fas_simulator PRIVATE /W4)
else()
    target_compile_options(fas_simulator PRIVATE -Wall -Wextra -Wpedantic)
endif()

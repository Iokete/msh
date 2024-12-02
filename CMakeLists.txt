cmake_minimum_required(VERSION 3.30)
project(msh C)

# Establecer el estándar de C
set(CMAKE_C_STANDARD 11)

# Incluir directorios para encontrar parser.h
include_directories(${CMAKE_SOURCE_DIR}/include)

# Añadir el ejecutable
add_executable(msh main.c
)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -no-pie")
# Especificar la biblioteca estática a enlazar
target_link_libraries(msh PRIVATE ${CMAKE_SOURCE_DIR}/lib/libparser.a)
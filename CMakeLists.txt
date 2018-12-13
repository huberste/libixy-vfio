cmake_minimum_required(VERSION 2.8)
project(libixy-vfio)

set(CMAKE_C_STANDARD 11)

add_compile_options(-g -O2 -march=native -fomit-frame-pointer -std=c11
	-D_XOPEN_SOURCE=700
	-D_DEFAULT_SOURCE
	-Wall
	-Wextra
	-Wno-unused-parameter
	-Wno-unused-function
	-Wformat=2
)

include_directories(
	${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_library(ixy-vfio SHARED src/libixy-vfio.c)

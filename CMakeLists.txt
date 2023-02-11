cmake_minimum_required(VERSION 3.0)
project(WebBench)

set(src 
    webbench.c
   )
set(CMAKE_CXX_COMPILER "g++")

add_executable(webbench ${src})



cmake_minimum_required(VERSION 2.8)
add_definitions(-std=gnu99)
add_executable(pass-trigger pass-trigger.c)
target_link_libraries(pass-trigger predict)

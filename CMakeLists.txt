cmake_minimum_required(VERSION 2.6)

project(cornerstone CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

if(WIN32)
    add_definitions(/D_UNICODE /DUNICODE /D_WIN32_WINNT=0x0501)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zi /vmm /vmg /MP /wo4067 /wo4244 /wo4267 /wo4181 /wd4099")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /INCREMENTAL:NO")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -pipe -std=c++11 -Wall")
endif()

string(TOUPPER ${CMAKE_BUILD_TYPE} BUILD_TYPE)
message(STATUS "CXX_FLAGS = " ${CMAKE_CXX_FLAGS} " " ${CMAKE_CXX_FLAGS_${BUILD_TYPE}})

set(Asio_INCLUDE_DIRS asio/asio/include)
include_directories(${Asio_INCLUDE_DIRS})
include_directories(${PROJECT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/include)

set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin) 

file(GLOB_RECURSE SOURCE 
     ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx
)

add_library(cornerstone ${SOURCE})
target_link_libraries(cornerstone )
set_target_properties(cornerstone PROPERTIES OUTPUT_NAME cornerstone)
install(TARGETS cornerstone DESTINATION lib)

add_subdirectory(tests)
add_subdirectory(examples)

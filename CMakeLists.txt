project(quabs)

cmake_minimum_required(VERSION 3.2) # need 3.9 for interprocedural optimization

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 11)

#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -flto")
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -flto")

#set(CMAKE_INTERPROCEDURAL_OPTIMIZATION True)

#set(CMAKE_BUILD_TYPE Release)

add_subdirectory(libsolve/src)
include_directories(libsolve/src)


add_subdirectory(src)

add_executable(quabs src/main.c)
target_link_libraries(quabs quabs-base)
target_link_libraries(quabs solve)
#target_link_libraries(quabs sat_picosat)
#target_link_libraries(quabs sat_minisat)
#target_link_libraries(quabs sat_lingeling)
#target_link_libraries(quabs sat_glucose)
target_link_libraries(quabs sat_cryptominisat)

add_executable(qcir2qaiger src/qcir2aig.c)
target_link_libraries(qcir2qaiger quabs-base)
target_link_libraries(qcir2qaiger solve)

add_executable(qaiger2qcir src/aig2qcir.c)
target_link_libraries(qaiger2qcir quabs-base)
target_link_libraries(qaiger2qcir solve)

add_executable(qcir2qdimacs src/qcir2qdimacs.c)
target_link_libraries(qcir2qdimacs quabs-base)
target_link_libraries(qcir2qdimacs solve)

add_executable(qcir2qcir src/qcir2qcir.c)
target_link_libraries(qcir2qcir quabs-base)
target_link_libraries(qcir2qcir solve)

find_package(Magnum REQUIRED
    Shaders
    Sdl2Application)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CORRADE_CXX_FLAGS}")
include_directories(${MAGNUM_INCLUDE_DIRS} ${MAGNUM_APPLICATION_INCLUDE_DIRS})

add_executable(serialplot serialplot.cpp)
target_link_libraries(serialplot
    ${MAGNUM_APPLICATION_LIBRARIES}
    ${MAGNUM_LIBRARIES}
    ${MAGNUM_SHADERS_LIBRARIES})

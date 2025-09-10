function(BuildStaticLibrary StaticLibraryName RootPath)
    file(GLOB_RECURSE SOURCE_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/src/*.cpp
    )

    file(GLOB_RECURSE HEADER_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/inc/*.h
            ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/inc/*.hpp
    )

    add_library(${StaticLibraryName} STATIC ${SOURCE_FILES} ${HEADER_FILES})
    target_link_libraries(${StaticLibraryName} PUBLIC ${ARGN})
    target_include_directories(${StaticLibraryName} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/${RootPath}/inc/)
endfunction()
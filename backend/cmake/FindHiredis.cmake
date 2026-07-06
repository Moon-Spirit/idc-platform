# Custom FindHiredis.cmake
# Finds hiredis from our deps directory.

find_path(HIREDIS_INCLUDE_DIR
    NAMES hiredis.h
    PATH_SUFFIXES hiredis
    PATHS ${CMAKE_PREFIX_PATH}/include
          /usr/include
          /usr/local/include
    NO_DEFAULT_PATH
)

find_library(HIREDIS_LIBRARY
    NAMES hiredis
    PATHS ${CMAKE_PREFIX_PATH}/lib
          /usr/lib
          /usr/local/lib
    NO_DEFAULT_PATH
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Hiredis DEFAULT_MSG HIREDIS_LIBRARY HIREDIS_INCLUDE_DIR)

mark_as_advanced(HIREDIS_INCLUDE_DIR HIREDIS_LIBRARY)

if(HIREDIS_FOUND)
    set(HIREDIS_INCLUDE_DIRS ${HIREDIS_INCLUDE_DIR})
    set(HIREDIS_LIBRARIES ${HIREDIS_LIBRARY})

    if(NOT TARGET Hiredis_lib)
        add_library(Hiredis_lib INTERFACE IMPORTED)
    endif()
    set_target_properties(Hiredis_lib
        PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${HIREDIS_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES "${HIREDIS_LIBRARIES}"
    )
    if(NOT HIREDIS_FIND_QUIETLY)
        message(STATUS "Found Hiredis: ${HIREDIS_LIBRARIES} (${HIREDIS_INCLUDE_DIRS})")
    endif()
else()
    set(HIREDIS_LIBRARIES)
    set(HIREDIS_INCLUDE_DIRS)
endif()

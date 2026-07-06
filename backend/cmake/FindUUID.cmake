# Custom FindUUID.cmake
# Finds the system libuuid (util-linux) via our deps directory.
# Overrides Drogon's bundled FindUUID.cmake which has issues finding
# the library on systems without uuid-dev installed.

find_path(UUID_INCLUDE_DIR
    NAMES uuid.h
    PATH_SUFFIXES uuid
    PATHS ${CMAKE_PREFIX_PATH}/include
          /usr/include
          /usr/local/include
    NO_DEFAULT_PATH
)

find_library(UUID_LIBRARY
    NAMES uuid
    PATHS ${CMAKE_PREFIX_PATH}/lib
          /usr/lib
          /usr/lib/aarch64-linux-gnu
          /usr/local/lib
    NO_DEFAULT_PATH
)

# Fallback
if(NOT UUID_INCLUDE_DIR)
    find_path(UUID_INCLUDE_DIR NAMES uuid.h PATH_SUFFIXES uuid)
endif()
if(NOT UUID_LIBRARY)
    find_library(UUID_LIBRARY NAMES uuid)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(UUID DEFAULT_MSG UUID_INCLUDE_DIR UUID_LIBRARY)

mark_as_advanced(UUID_INCLUDE_DIR UUID_LIBRARY)

if(UUID_FOUND)
    set(UUID_INCLUDE_DIRS ${UUID_INCLUDE_DIR})
    set(UUID_LIBRARIES ${UUID_LIBRARY})

    if(NOT TARGET UUID_lib)
        add_library(UUID_lib INTERFACE IMPORTED)
    endif()
    set_target_properties(UUID_lib
        PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${UUID_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES "${UUID_LIBRARIES}"
    )
    if(NOT UUID_FIND_QUIETLY)
        message(STATUS "Found UUID: ${UUID_LIBRARIES} (${UUID_INCLUDE_DIRS})")
    endif()
else()
    set(UUID_LIBRARIES)
    set(UUID_INCLUDE_DIRS)
endif()

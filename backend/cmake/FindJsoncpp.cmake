# Custom FindJsoncpp.cmake
# Overrides the one shipped with Drogon so we can point to a local install.
# This file is found first because we prepend CMAKE_MODULE_PATH.

find_path(JSONCPP_INCLUDE_DIRS
    NAMES json/json.h
    PATHS ${Jsoncpp_DIR} ${CMAKE_PREFIX_PATH}/include
    PATH_SUFFIXES jsoncpp
    NO_DEFAULT_PATH
)

find_library(JSONCPP_LIBRARIES
    NAMES jsoncpp
    PATHS ${Jsoncpp_DIR} ${CMAKE_PREFIX_PATH}/lib
    NO_DEFAULT_PATH
)

# Fallback to system paths
if(NOT JSONCPP_INCLUDE_DIRS OR NOT JSONCPP_LIBRARIES)
    find_path(JSONCPP_INCLUDE_DIRS NAMES json/json.h PATH_SUFFIXES jsoncpp)
    find_library(JSONCPP_LIBRARIES NAMES jsoncpp)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Jsoncpp
    DEFAULT_MSG
    JSONCPP_INCLUDE_DIRS
    JSONCPP_LIBRARIES
)

mark_as_advanced(JSONCPP_INCLUDE_DIRS JSONCPP_LIBRARIES)

if(Jsoncpp_FOUND)
    if(NOT TARGET Jsoncpp_lib)
        add_library(Jsoncpp_lib INTERFACE IMPORTED)
    endif()
    set_target_properties(Jsoncpp_lib
        PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${JSONCPP_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES "${JSONCPP_LIBRARIES}"
    )
    if(NOT Jsoncpp_FIND_QUIETLY)
        message(STATUS "FindJsoncpp: ${JSONCPP_INCLUDE_DIRS} -> ${JSONCPP_LIBRARIES}")
    endif()
endif()

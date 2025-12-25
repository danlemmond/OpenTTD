# Find libvterm library
#
# This module defines:
#  Vterm_FOUND - System has libvterm
#  Vterm_INCLUDE_DIRS - The libvterm include directories
#  Vterm_LIBRARIES - The libraries needed to use libvterm

# Try pkg-config first
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(PC_VTERM QUIET vterm)
endif()

# Find the include directory
find_path(Vterm_INCLUDE_DIR
    NAMES vterm.h
    HINTS
        ${PC_VTERM_INCLUDEDIR}
        ${PC_VTERM_INCLUDE_DIRS}
        /opt/homebrew/opt/libvterm/include
        /usr/local/opt/libvterm/include
        /opt/local/include
    PATH_SUFFIXES vterm
)

# Find the library
find_library(Vterm_LIBRARY
    NAMES vterm libvterm
    HINTS
        ${PC_VTERM_LIBDIR}
        ${PC_VTERM_LIBRARY_DIRS}
        /opt/homebrew/opt/libvterm/lib
        /usr/local/opt/libvterm/lib
        /opt/local/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Vterm
    REQUIRED_VARS Vterm_LIBRARY Vterm_INCLUDE_DIR
)

if(Vterm_FOUND)
    set(Vterm_LIBRARIES ${Vterm_LIBRARY})
    set(Vterm_INCLUDE_DIRS ${Vterm_INCLUDE_DIR})

    if(NOT TARGET Vterm::Vterm)
        add_library(Vterm::Vterm UNKNOWN IMPORTED)
        set_target_properties(Vterm::Vterm PROPERTIES
            IMPORTED_LOCATION "${Vterm_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${Vterm_INCLUDE_DIR}"
        )
    endif()
endif()

mark_as_advanced(Vterm_INCLUDE_DIR Vterm_LIBRARY)

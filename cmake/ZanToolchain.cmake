# Shared location policy for external toolchains (MinGW, LLVM, DevEco, ...).
#
# One probe order for every caller, so a new script cannot grow its own
# diverging candidate table:
#
#   1. explicit override -- the caller's CMake cache variable / -D value, or
#      the environment variable names passed in ENV.
#   2. PATH -- the tool executable itself (find_program), then its
#      <root>/bin directory gives the installation root.
#   3. standard install locations -- the caller's ROOTS list, typically built
#      from environment bases (SystemDrive / ProgramFiles / LOCALAPPDATA /
#      USERPROFILE) instead of a hard-coded drive letter.
#
# A miss is reported through zan_probe_failure_message(): what was searched,
# and which variable the user should set. Callers decide whether that is fatal
# (configure-time dependencies) or a warning (optional bundles).

include_guard(GLOBAL)

# Remove a trailing slash and unify separators so `"${root}/${sentinel}"` never
# produces `//` or a mixed `C:/foo\bar`.
function(_zan_clean_path in out)
    file(TO_CMAKE_PATH "${in}" _p)
    string(REGEX REPLACE "[/]+" "/" _p "${_p}")
    string(REGEX REPLACE "/$" "" _p "${_p}")
    set(${out} "${_p}" PARENT_SCOPE)
endfunction()

# zan_resolve_root(<out_var>
#                  NAME <human name>            # used in diagnostics
#                  SENTINEL <relative path>     # must exist under the root
#                  ENV <VAR...>                 # explicit overrides, first wins
#                  PATH_TOOLS <exe...>          # PATH lookup; root = parent-of-bin
#                  ROOTS <dir...>)              # known install roots
#
# On success <out_var> is the clean root and <out_var>_FROM says which
# candidate matched. On failure it is empty and <out_var>_TRIED holds the list.
function(zan_resolve_root out_var)
    cmake_parse_arguments(ZR "" "NAME;SENTINEL" "ENV;PATH_TOOLS;ROOTS" ${ARGN})
    set(_tried "")
    set(_name "${ZR_NAME}")
    if("${_name}" STREQUAL "")
        set(_name "toolchain")
    endif()

    # 1) Explicit environment overrides.
    foreach(_var IN LISTS ZR_ENV)
        if(DEFINED ENV{${_var}} AND NOT "$ENV{${_var}}" STREQUAL "")
            _zan_clean_path("$ENV{${_var}}" _root)
            list(APPEND _tried "${_var}=${_root}")
            if(EXISTS "${_root}/${ZR_SENTINEL}")
                message(STATUS "${_name}: ${_root} (from ${_var})")
                set(${out_var} "${_root}" PARENT_SCOPE)
                set(${out_var}_FROM "${_var}" PARENT_SCOPE)
                set(${out_var}_TRIED "${_tried}" PARENT_SCOPE)
                set(${out_var}_NAME "${_name}" PARENT_SCOPE)
                set(${out_var}_ENV "${ZR_ENV}" PARENT_SCOPE)
                return()
            endif()
        endif()
    endforeach()

    # 2) PATH: a tool in <root>/bin identifies the root, regardless of drive.
    if(ZR_PATH_TOOLS)
        find_program(_zan_probe_tool NAMES ${ZR_PATH_TOOLS})
        if(_zan_probe_tool)
            get_filename_component(_bin "${_zan_probe_tool}" DIRECTORY)
            get_filename_component(_root "${_bin}" DIRECTORY)
            _zan_clean_path("${_root}" _root)
            list(APPEND _tried "PATH: ${_zan_probe_tool}")
            if(EXISTS "${_root}/${ZR_SENTINEL}")
                message(STATUS "${_name}: ${_root} (from ${_zan_probe_tool})")
                set(${out_var} "${_root}" PARENT_SCOPE)
                set(${out_var}_FROM "${_zan_probe_tool}" PARENT_SCOPE)
                set(${out_var}_TRIED "${_tried}" PARENT_SCOPE)
                set(${out_var}_NAME "${_name}" PARENT_SCOPE)
                set(${out_var}_ENV "${ZR_ENV}" PARENT_SCOPE)
                return()
            endif()
        endif()
    endif()

    # 3) Standard install roots.
    foreach(_root IN LISTS ZR_ROOTS)
        if("${_root}" STREQUAL "")
            continue()
        endif()
        _zan_clean_path("${_root}" _root)
        list(APPEND _tried "${_root}")
        if(EXISTS "${_root}/${ZR_SENTINEL}")
            message(STATUS "${_name}: ${_root}")
            set(${out_var} "${_root}" PARENT_SCOPE)
            set(${out_var}_FROM "${_root}" PARENT_SCOPE)
            set(${out_var}_TRIED "${_tried}" PARENT_SCOPE)
            set(${out_var}_NAME "${_name}" PARENT_SCOPE)
            set(${out_var}_ENV "${ZR_ENV}" PARENT_SCOPE)
            return()
        endif()
    endforeach()

    set(${out_var} "" PARENT_SCOPE)
    set(${out_var}_FROM "" PARENT_SCOPE)
    set(${out_var}_TRIED "${_tried}" PARENT_SCOPE)
    set(${out_var}_NAME "${_name}" PARENT_SCOPE)
    set(${out_var}_ENV "${ZR_ENV}" PARENT_SCOPE)
endfunction()

# Render the last zan_resolve_root() failure as a multi-line, actionable
# message. `prefix` is the out_var that was passed to zan_resolve_root.
function(zan_probe_failure_message prefix out_var)
    set(_name "${${prefix}_NAME}")
    set(_tried "${${prefix}_TRIED}")
    set(_env "${${prefix}_ENV}")
    if("${_name}" STREQUAL "")
        set(_name "${prefix}")
    endif()
    if(_tried)
        string(REPLACE ";" "\n    " _tried_text "${_tried}")
    else()
        set(_tried_text "(no candidates)")
    endif()
    if(_env)
        string(REPLACE ";" " / " _env_text "${_env}")
        set(_hint "Set ${_env_text} to the installation root.")
    else()
        set(_hint "Install it, or pass an explicit path.")
    endif()
    set(${out_var}
        "${_name} not found. Tried:\n    ${_tried_text}\n${_hint}"
        PARENT_SCOPE)
endfunction()

# Candidate roots for MinGW-w64, derived from where Windows actually puts
# installs, not from one author's drive letter. Covers MSYS2 (all three
# environments), TDM-GCC, plain mingw-w64, scoop and local user installs.
function(zan_mingw_roots out_var)
    set(_roots "")
    set(_bases "")
    foreach(_e SystemDrive ProgramFiles ProgramW6432 LOCALAPPDATA USERPROFILE)
        if(DEFINED ENV{${_e}} AND NOT "$ENV{${_e}}" STREQUAL "")
            list(APPEND _bases "$ENV{${_e}}")
        endif()
    endforeach()
    foreach(_base IN LISTS _bases)
        _zan_clean_path("${_base}" _base)
        foreach(_sub
                "msys64/mingw64" "msys64/ucrt64" "msys64/clang64"
                "TDM-GCC-64" "mingw64" "mingw32" "mingw"
                "scoop/apps/mingw/current" "Programs/mingw64")
            list(APPEND _roots "${_base}/${_sub}")
        endforeach()
    endforeach()
    set(${out_var} "${_roots}" PARENT_SCOPE)
endfunction()

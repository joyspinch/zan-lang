# Build-time acquisition of the CEF C API headers, plus the two zan_cef driver
# variants built against them.
#
# Why two variants: CEF's C structs are version-bound, and which branch runs on
# the user's machine is decided at run time by Gui.Component.CefBrowser's
# CefRuntime (Windows 7/8/8.1 can only use 109, everything else uses the pinned
# current branch). The driver dlopens libcef, so the only version coupling is
# these headers -- one build per branch, and CefBackend loads the matching one
# (zan_cef109 / zan_cef).
#
# Why download instead of vendoring: the header set is ~1.5k files that must
# match the runtime exactly, so it is fetched from the same official archive the
# runtime comes from (the "minimal" package, which carries include/) into the
# build tree. Nothing enters the repository.
#
# Which CEF the driver is built against has exactly one source of truth: the
# pinned table in stdlib/Gui/Component/CefBrowser/CefRuntime.zan, which is also
# what a program downloads at run time. Spelling the version here as well let
# the two drift (headers 151.3.18 vs runtime 151.3.23), and libcef then refuses
# to load with "cef api hash mismatch", so it is read out of that file instead.
set(ZAN_CEF_CDN "https://cef-builds.spotifycdn.com"
    CACHE STRING "Base URL of the official CEF build CDN")

# The C API version the modern variant is compiled against. Pinning it is what
# makes the driver survive a runtime upgrade: cef_api_hash() is computed per API
# version, so a driver built at 15101 loads in every libcef whose supported
# range covers 15101. Leaving it unset selects CEF_API_VERSION_EXPERIMENTAL
# (999999), whose hash is tied to one exact CEF build - which is how the
# mismatch above happened.
set(ZAN_CEF_API_VERSION "15101"
    CACHE STRING "CEF C API version zan_cef is compiled against")

# Pull "<branch>..." out of CefRuntime.zan: <prefix_fn> is the branch accessor
# (CurrentBranch / LegacyBranch), and the version is the first entry of the
# pinned table on that branch.
function(zan_cef_pinned_version prefix_fn out_var)
    file(READ "${CMAKE_SOURCE_DIR}/stdlib/Gui/Component/CefBrowser/CefRuntime.zan"
         _src)
    string(REGEX MATCH
           "${prefix_fn}\\(\\)[ \t]*{[ \t]*return[ \t]*\"([0-9]+\\.)\""
           _unused "${_src}")
    set(_branch "${CMAKE_MATCH_1}")
    if(_branch STREQUAL "")
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()
    string(REPLACE "." "\\." _branch_re "${_branch}")
    string(REGEX MATCH "\"(${_branch_re}[0-9][^\"]*\\+chromium-[^\"]*)\""
           _unused2 "${_src}")
    set(${out_var} "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()

zan_cef_pinned_version("CurrentBranch" ZAN_CEF_VERSION)
zan_cef_pinned_version("LegacyBranch" ZAN_CEF_VERSION_LEGACY)
if(ZAN_CEF_VERSION STREQUAL "" OR ZAN_CEF_VERSION_LEGACY STREQUAL "")
    message(FATAL_ERROR
            "cannot read the pinned CEF versions out of CefRuntime.zan")
endif()

# Official platform tag of the host, i.e. which archive carries our headers.
# The headers themselves are platform-independent in practice, but taking the
# host's archive keeps a developer's download shared with the runtime cache.
function(zan_cef_platform out)
    if(WIN32)
        set(${out} "windows64" PARENT_SCOPE)
    elseif(APPLE)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
            set(${out} "macosarm64" PARENT_SCOPE)
        else()
            set(${out} "macosx64" PARENT_SCOPE)
        endif()
    else()
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
            set(${out} "linuxarm64" PARENT_SCOPE)
        else()
            set(${out} "linux64" PARENT_SCOPE)
        endif()
    endif()
endfunction()

# Make ${ZAN_CEF_HEADERS_<tag>} point at a directory containing `include/` for
# `version`, downloading and unpacking the official minimal archive if needed.
# An already-unpacked runtime (the cache CefRuntime fills, or an explicit
# -DZAN_CEF_HEADERS_DIR=...) is reused as-is: the archive is a few hundred MB
# and the runtime download has the same include/.
function(zan_cef_headers version tag out_var)
    if(ZAN_CEF_HEADERS_DIR AND EXISTS "${ZAN_CEF_HEADERS_DIR}/include/cef_version.h")
        set(${out_var} "${ZAN_CEF_HEADERS_DIR}" PARENT_SCOPE)
        return()
    endif()
    zan_cef_platform(plat)
    # Directory naming mirrors CefRuntime.RuntimeDir(): "<platform>-<version>"
    # with '+' turned into '-', so a runtime already installed by a Zan program
    # is found here instead of downloading it twice.
    string(REPLACE "+" "-" vdir "${version}")
    set(candidates "")
    if(DEFINED ENV{ZAN_CEF_CACHE})
        list(APPEND candidates "$ENV{ZAN_CEF_CACHE}/${plat}-${vdir}")
    endif()
    if(UNIX AND NOT APPLE)
        list(APPEND candidates "$ENV{HOME}/.cache/zan/cef/${plat}-${vdir}")
    elseif(APPLE)
        list(APPEND candidates "$ENV{HOME}/Library/Caches/Zan/cef/${plat}-${vdir}")
    else()
        list(APPEND candidates "$ENV{LOCALAPPDATA}/Zan/cef/${plat}-${vdir}")
    endif()
    foreach(cand IN LISTS candidates)
        if(EXISTS "${cand}/include/cef_version.h")
            message(STATUS "CEF headers ${version}: reusing ${cand}")
            set(${out_var} "${cand}" PARENT_SCOPE)
            return()
        endif()
    endforeach()

    # Keyed by version, not just by tag: a bump must not silently reuse the
    # previous branch's headers left in the build tree.
    set(dest "${CMAKE_BINARY_DIR}/cef-headers/${tag}-${vdir}")
    if(EXISTS "${dest}/include/cef_version.h")
        set(${out_var} "${dest}" PARENT_SCOPE)
        return()
    endif()

    set(archive_name "cef_binary_${version}_${plat}_minimal.tar.bz2")
    # '+' is a literal in the CDN's paths but means space when unescaped.
    string(REPLACE "+" "%2B" archive_url_name "${archive_name}")
    set(archive "${CMAKE_BINARY_DIR}/cef-headers/${archive_name}")
    if(NOT EXISTS "${archive}")
        message(STATUS "CEF headers ${version}: downloading ${archive_name}")
        file(DOWNLOAD "${ZAN_CEF_CDN}/${archive_url_name}" "${archive}"
             STATUS dl_status SHOW_PROGRESS TLS_VERIFY ON)
        list(GET dl_status 0 dl_code)
        if(NOT dl_code EQUAL 0)
            list(GET dl_status 1 dl_msg)
            file(REMOVE "${archive}")
            message(WARNING "CEF headers ${version}: download failed (${dl_msg})")
            set(${out_var} "" PARENT_SCOPE)
            return()
        endif()
    endif()

    # Unpack only include/: the rest of the archive is the runtime, which is not
    # a build input (Zan programs fetch it per machine at run time).
    string(REGEX REPLACE "\\.tar\\.bz2$" "" archive_root "${archive_name}")
    set(staging "${CMAKE_BINARY_DIR}/cef-headers/${tag}.staging")
    file(REMOVE_RECURSE "${staging}")
    file(MAKE_DIRECTORY "${staging}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E tar xjf "${archive}"
                "${archive_root}/include"
        WORKING_DIRECTORY "${staging}"
        RESULT_VARIABLE untar_code
        OUTPUT_QUIET ERROR_VARIABLE untar_err)
    if(NOT untar_code EQUAL 0 OR
       NOT EXISTS "${staging}/${archive_root}/include/cef_version.h")
        file(REMOVE_RECURSE "${staging}")
        message(WARNING "CEF headers ${version}: unpack failed (${untar_err})")
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()
    file(REMOVE_RECURSE "${dest}")
    get_filename_component(dest_parent "${dest}" DIRECTORY)
    file(MAKE_DIRECTORY "${dest_parent}")
    file(RENAME "${staging}/${archive_root}" "${dest}")
    file(REMOVE_RECURSE "${staging}")
    message(STATUS "CEF headers ${version}: unpacked into ${dest}")
    set(${out_var} "${dest}" PARENT_SCOPE)
endfunction()

# Driver directory name of the host, spelled exactly as zan_driver_subdir() in
# src/compiler/main.c: zanc looks the bundle up under that name.
function(zan_cef_driver_subdir out)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
        set(_arch "arm64")
    else()
        set(_arch "x64")
    endif()
    if(WIN32)
        set(${out} "win-${_arch}" PARENT_SCOPE)
    elseif(APPLE)
        set(${out} "macos-${_arch}" PARENT_SCOPE)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "riscv64")
        set(${out} "linux-riscv64" PARENT_SCOPE)
    else()
        set(${out} "linux-${_arch}" PARENT_SCOPE)
    endif()
endfunction()

# One driver variant: same source, different header set / output name.
function(zan_cef_add_variant target version tag)
    zan_cef_headers("${version}" "${tag}" headers)
    if(headers STREQUAL "")
        message(WARNING "skipping ${target} (no CEF ${version} headers)")
        return()
    endif()
    add_library(${target} SHARED
        "${CMAKE_SOURCE_DIR}/stdlib/Gui/Component/CefBrowser/native/zan_cef.c")
    target_include_directories(${target} PRIVATE "${headers}")
    if(tag STREQUAL "legacy")
        # CEF 109 predates the versioned C API (no CEF_API_VERSION, unsized
        # structs, single-argument cef_api_hash).
        target_compile_definitions(${target} PRIVATE ZAN_CEF_LEGACY)
    else()
        target_compile_definitions(${target}
                                   PRIVATE CEF_API_VERSION=${ZAN_CEF_API_VERSION})
    endif()
    # libcef itself is never linked: the driver resolves it out of the runtime
    # directory at run time, because which CEF is installed is a per-machine
    # decision (and a missing runtime must degrade to a placeholder, not a
    # failed process start).
    if(WIN32)
        target_link_libraries(${target} PRIVATE user32)
    elseif(NOT APPLE)
        target_link_libraries(${target} PRIVATE dl)
    endif()
    # zanc bundles a run-time loaded driver from the directory of the stdlib
    # module owning it (stdlib/Gui/Component/CefBrowser/drivers/<target>, see
    # its driver.manifest), so the built library is staged there. Without that
    # copy nothing lands beside the produced executable and CefBackend reports
    # "找不到原生 zan_cef driver" on a machine with no system-wide install.
    zan_cef_driver_subdir(_sub)
    set(_drv
        "${CMAKE_SOURCE_DIR}/stdlib/Gui/Component/CefBrowser/drivers/${_sub}")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_drv}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:${target}>"
                "${_drv}/$<TARGET_FILE_NAME:${target}>"
        COMMENT "staging ${target} into ${_drv}")
    set(ZAN_CEF_AVAILABLE TRUE PARENT_SCOPE)
endfunction()

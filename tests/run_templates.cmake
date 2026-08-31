# Compile-checks every project template the New Project wizard can scaffold.
#
#   cmake -DZANC=<zanc> -DTEMPLATES=<templates dir> -DWORK=<scratch dir>
#         [-DSTDLIB=<stdlib dir>] [-DEXE_EXT=.exe] -P run_templates.cmake
#
# A template is only useful if a freshly scaffolded project compiles, and the
# templates are plain source on disk that nothing else in the suite touches:
# a stdlib API rename silently rots them until a user hits the error in the
# IDE. So scaffold each one exactly the way ZanIDE.CreateProject does --
# substitute {{NAME}} in file *contents*, then rename the GUI pair
# src/App.zan + src/App.zform to the project name -- and build it.

cmake_policy(SET CMP0012 NEW)

if(NOT ZANC OR NOT TEMPLATES OR NOT WORK)
  message(FATAL_ERROR "run_templates.cmake: ZANC, TEMPLATES and WORK are required")
endif()

set(NAME "TplProbe")

file(REMOVE_RECURSE ${WORK})
file(MAKE_DIRECTORY ${WORK})

# Discover every project manifest, including nested wizard templates such as
# gui/gui-free/NewWeb.  A non-recursive two-level glob silently skipped those
# projects and made the compile gate weaker than the IDE's project picker.
file(GLOB_RECURSE _projs LIST_DIRECTORIES false ${TEMPLATES}/zan.proj)
list(SORT _projs)
if(_projs STREQUAL "")
  message(FATAL_ERROR "no templates found under ${TEMPLATES}")
endif()

set(_failed "")
foreach(_proj IN LISTS _projs)
  get_filename_component(_dir ${_proj} DIRECTORY)
  file(RELATIVE_PATH _rel ${TEMPLATES} ${_dir})
  string(REPLACE "/" "_" _slug ${_rel})
  set(_out ${WORK}/${_slug})

  file(READ ${_proj} _manifest)
  set(_type "")
  set(_target "exe")
  set(_entry "")
  # Accept both the canonical one-key-per-line manifest and the compact
  # semicolon-separated form used by older templates.  Stop at either a
  # semicolon or line ending so following keys cannot become part of a value.
  if(_manifest MATCHES "type[ \t]*=[ \t]*([^;\r\n]*)")
    string(STRIP "${CMAKE_MATCH_1}" _type)
  endif()
  if(_manifest MATCHES "target[ \t]*=[ \t]*([^;\r\n]*)")
    string(STRIP "${CMAKE_MATCH_1}" _target)
  endif()
  if(_manifest MATCHES "entry[ \t]*=[ \t]*([^;\r\n]*)")
    string(STRIP "${CMAKE_MATCH_1}" _entry)
  endif()
  string(REPLACE "{{NAME}}" "${NAME}" _entry "${_entry}")

  # Scaffold: copy the tree, substituting {{NAME}} in text file contents.
  file(GLOB_RECURSE _files RELATIVE ${_dir} ${_dir}/*)
  foreach(_f IN LISTS _files)
    if(_f STREQUAL "template.manifest" OR _f STREQUAL "manifest.txt")
      continue()
    endif()
    get_filename_component(_ext ${_f} EXT)
    if(_ext MATCHES "^\\.(zan|zform|proj|json|css|html|md|txt)$")
      file(READ ${_dir}/${_f} _body)
      string(REPLACE "{{NAME}}" "${NAME}" _body "${_body}")
      file(WRITE ${_out}/${_f} "${_body}")
    else()
      configure_file(${_dir}/${_f} ${_out}/${_f} COPYONLY)
    endif()
  endforeach()

  if(_type STREQUAL "gui")
    foreach(_ext zan zform)
      if(EXISTS ${_out}/src/App.${_ext})
        file(RENAME ${_out}/src/App.${_ext} ${_out}/src/${NAME}.${_ext})
      endif()
    endforeach()
    if(EXISTS ${_out}/src/${NAME}.zform)
      set(_entry "src/${NAME}.zform")
    endif()
  endif()

  if(NOT EXISTS ${_out}/${_entry})
    list(APPEND _failed "${_rel}: manifest entry '${_entry}' does not exist")
    continue()
  endif()

  # The .zform entry must come first: zanc emits Main from the first design.
  set(_srcs "")
  if(_entry MATCHES "\\.zform$")
    list(APPEND _srcs ${_out}/${_entry})
  endif()
  file(GLOB_RECURSE _zan ${_out}/src/*.zan)
  list(SORT _zan)
  list(APPEND _srcs ${_zan})
  list(REMOVE_DUPLICATES _srcs)

  set(_args "")
  if(STDLIB)
    list(APPEND _args --stdlib-path ${STDLIB})
  else()
    list(APPEND _args --auto-stdlib)
  endif()
  if(_target STREQUAL "dll")
    set(_artifact ${_out}/out.dll)
    list(APPEND _args --emit-lib)
  else()
    set(_artifact ${_out}/out${EXE_EXT})
  endif()
  if(_type STREQUAL "gui")
    if(CMAKE_HOST_WIN32)
      list(APPEND _args --subsystem windows)
    endif()
    if(DRIVER_DIR)
      # Link the freshly built zan_gui, not the committed driver bundle
      # (which can lag behind newly added native exports).
      list(APPEND _args --driver-dir ${DRIVER_DIR})
    endif()
  endif()

  execute_process(
    COMMAND ${ZANC} ${_srcs} -o ${_artifact} ${_args}
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _o
    ERROR_VARIABLE  _e)
  if(NOT _rc EQUAL 0)
    list(APPEND _failed "${_rel}:\n${_o}${_e}")
  else()
    message(STATUS "OK: ${_rel}")
  endif()
endforeach()

if(NOT _failed STREQUAL "")
  string(REPLACE ";" "\n\n" _report "${_failed}")
  message(FATAL_ERROR "template(s) failed to build:\n\n${_report}")
endif()

# Compile-checks every project template the New Project wizard can scaffold.
#
#   cmake -DZANC=<zanc> -DTEMPLATES=<templates dir> -DWORK=<scratch dir>
#         [-DSTDLIB=<stdlib dir>] [-DEXE_EXT=.exe]
#         [-DFILTER=^gui/] [-DCOMPILE_TIMEOUT=600] -P run_templates.cmake
#
# FILTER is a case-sensitive CMake regular expression matched against the
# slash-separated template path (e.g. game/snake); use ^game/snake$ for an
# exact match. EXTRA_PROJECTS use their directory basename (e.g. html_gallery).
# An omitted/empty FILTER selects everything, including EXTRA_PROJECTS.
# COMPILE_TIMEOUT is a positive number of seconds per compiler invocation.
# Only selected project directories are reset. Each contains stdout.log,
# stderr.log and result.json (running/pass/fail/timeout); WORK/results.json
# is the current invocation's index, checkpointed before and after each item.
# These are diagnostics, not a cache: selected projects always compile again.
#
# A template is only useful if a freshly scaffolded project compiles, and the
# templates are plain source on disk that nothing else in the suite touches:
# a stdlib API rename silently rots them until a user hits the error in the
# IDE. So scaffold each one exactly the way ZanIDE.CreateProject does --
# substitute {{NAME}} in file *contents*, then rename the GUI pair
# src/App.zan + src/App.html to the project name -- and build it.

cmake_policy(SET CMP0012 NEW)
cmake_policy(SET CMP0057 NEW)

if(NOT ZANC OR NOT TEMPLATES OR NOT WORK)
  message(FATAL_ERROR "run_templates.cmake: ZANC, TEMPLATES and WORK are required")
endif()

if(NOT DEFINED COMPILE_TIMEOUT)
  set(COMPILE_TIMEOUT 600)
endif()
if(NOT COMPILE_TIMEOUT MATCHES "^[0-9]+([.][0-9]+)?$" OR COMPILE_TIMEOUT LESS_EQUAL 0)
  message(FATAL_ERROR "COMPILE_TIMEOUT must be a positive number of seconds")
endif()

set(NAME "TplProbe")
get_filename_component(TEMPLATES "${TEMPLATES}" ABSOLUTE)
file(TO_CMAKE_PATH "${TEMPLATES}" TEMPLATES)
file(MAKE_DIRECTORY "${WORK}")

# Encode text at the JSON boundary, including compiler launch errors.
function(template_json_string out value)
  string(REPLACE "\\" "\\\\" value "${value}")
  string(REPLACE "\"" "\\\"" value "${value}")
  foreach(code RANGE 1 31)
    string(ASCII ${code} control)
    math(EXPR hex "${code}" OUTPUT_FORMAT HEXADECIMAL)
    string(SUBSTRING "${hex}" 2 -1 hex)
    if(code LESS 16)
      set(hex "0${hex}")
    endif()
    string(REPLACE "${control}" "\\u00${hex}" value "${value}")
  endforeach()
  set(${out} "\"${value}\"" PARENT_SCOPE)
endfunction()

# A macro intentionally updates the current invocation's index in place.
macro(template_result status result)
  set(_row "{}")
  foreach(_field template status result stdout stderr artifact)
    if(_field STREQUAL "template")
      set(_value "${_rel}")
    elseif(_field STREQUAL "status")
      set(_value "${status}")
    elseif(_field STREQUAL "result")
      set(_value "${result}")
    elseif(_field STREQUAL "artifact")
      set(_value "${_artifact}")
    else()
      set(_value "${_out}/${_field}.log")
    endif()
    template_json_string(_encoded "${_value}")
    string(JSON _row SET "${_row}" "${_field}" "${_encoded}")
  endforeach()
  file(WRITE "${_out}/result.json" "${_row}\n")
  string(JSON _results SET "${_results}" rows ${_index} "${_row}")
  file(WRITE "${WORK}/results.json" "${_results}\n")
endmacro()

# Discover every project manifest.  The glob must match the IDE's project
# picker exactly -- LoadDiskTemplates (src/ide_zan/src/shell) scans
# <category>/<template>/template.manifest, two levels, nothing deeper -- so a
# two-level glob here keeps this gate as strong as the picker, and no
# stronger: a nested directory that carries a zan.proj but no template
# manifest cannot be scaffolded by the picker, so compiling it here would
# only gate on something no user can reach.  A future template promoted into
# a real two-level directory enters the gate automatically.
file(GLOB _projs ${TEMPLATES}/*/*/zan.proj)
list(SORT _projs)
if(EXTRA_PROJECTS)
  list(APPEND _projs ${EXTRA_PROJECTS})
endif()
list(REMOVE_DUPLICATES _projs)

# Select the complete batch before touching any project working directory.
set(_selected "")
set(_slugs "")
foreach(_proj IN LISTS _projs)
  get_filename_component(_proj "${_proj}" ABSOLUTE)
  get_filename_component(_dir "${_proj}" DIRECTORY)
  file(RELATIVE_PATH _rel "${TEMPLATES}" "${_dir}")
  if(_rel MATCHES "^[.][.](/|$)")
    get_filename_component(_rel "${_dir}" NAME)
  endif()
  if(DEFINED FILTER AND NOT "${FILTER}" STREQUAL "" AND NOT _rel MATCHES "${FILTER}")
    continue()
  endif()
  string(REPLACE "/" "_" _slug "${_rel}")
  if(_slug IN_LIST _slugs)
    message(FATAL_ERROR "template working-directory collision: ${_rel}")
  endif()
  list(APPEND _slugs "${_slug}")
  list(APPEND _selected "${_proj}")
endforeach()
list(LENGTH _selected _total)
set(_results "{\"rows\":[],\"complete\":false,\"selected\":${_total}}")
file(WRITE "${WORK}/results.json" "${_results}\n")
if(_total EQUAL 0)
  message(FATAL_ERROR "no templates matched FILTER='${FILTER}' under ${TEMPLATES}")
endif()

set(_failed "")
foreach(_proj IN LISTS _selected)
  get_filename_component(_dir "${_proj}" DIRECTORY)
  file(RELATIVE_PATH _rel "${TEMPLATES}" "${_dir}")
  if(_rel MATCHES "^[.][.](/|$)")
    get_filename_component(_rel "${_dir}" NAME)
  endif()
  string(REPLACE "/" "_" _slug "${_rel}")
  set(_out "${WORK}/${_slug}")
  file(REMOVE_RECURSE "${_out}")
  file(MAKE_DIRECTORY "${_out}")
  file(WRITE "${_out}/stdout.log" "")
  file(WRITE "${_out}/stderr.log" "")
  set(_artifact "")
  string(JSON _index LENGTH "${_results}" rows)
  template_result("running" "not completed")
  message(STATUS "START: ${_rel} (timeout=${COMPILE_TIMEOUT}s, logs=${_out})")

  if(NOT EXISTS "${_proj}")
    set(_reason "manifest does not exist: ${_proj}")
    file(WRITE "${_out}/stderr.log" "${_reason}\n")
    template_result("fail" "${_reason}")
    list(APPEND _failed "${_rel}: ${_reason}")
    continue()
  endif()
  file(READ "${_proj}" _manifest)
  # Platform selection belongs to the scaffolded project, not the reusable template.
  if(_manifest MATCHES "(^|[;\r\n])[ \t]*(platform|publishPlatforms)[ \t]*=[ \t]*[^; \t\r\n]")
    set(_reason "template must not pin a platform; the wizard writes the selected targets")
    file(WRITE "${_out}/stderr.log" "${_reason}\n")
    template_result("fail" "${_reason}")
    list(APPEND _failed "${_rel}: ${_reason}")
    continue()
  endif()
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
    if(_ext MATCHES "^\\.(zan|proj|json|css|html|md|txt)$")
      file(READ ${_dir}/${_f} _body)
      string(REPLACE "{{NAME}}" "${NAME}" _body "${_body}")
      file(WRITE ${_out}/${_f} "${_body}")
    else()
      configure_file(${_dir}/${_f} ${_out}/${_f} COPYONLY)
    endif()
  endforeach()

  if(_type STREQUAL "gui")
    foreach(_ext zan html)
      if(EXISTS ${_out}/src/App.${_ext})
        file(RENAME ${_out}/src/App.${_ext} ${_out}/src/${NAME}.${_ext})
      endif()
    endforeach()
    if(_entry STREQUAL "src/App.zan" AND EXISTS ${_out}/src/${NAME}.zan)
      set(_entry "src/${NAME}.zan")
    endif()
  endif()

  if(NOT EXISTS ${_out}/${_entry})
    set(_reason "manifest entry '${_entry}' does not exist")
    file(WRITE "${_out}/stderr.log" "${_reason}\n")
    template_result("fail" "${_reason}")
    list(APPEND _failed "${_rel}: ${_reason}")
    continue()
  endif()

  # The entry is ALWAYS the first input: zanc emits Main from the first
  # input on the command line (for a .zan entry the entry's own Main must win).
  # Compile-time HTML generation channel has been removed (de208fe98): design
  # documents (.html) are runtime assets loaded via Html.Parse (Form.Show /
  # Form.ReadDoc, B-ID149) or baked in via --embed; they are NOT compiler
  # inputs.
  set(_srcs "")
  get_filename_component(_entry_norm "${_out}/${_entry}" ABSOLUTE)
  file(TO_CMAKE_PATH "${_entry_norm}" _entry_norm)
  list(APPEND _srcs ${_entry_norm})
  file(GLOB_RECURSE _zan ${_out}/src/*.zan)
  list(SORT _zan)
  foreach(_zf IN LISTS _zan)
    get_filename_component(_zf_norm "${_zf}" ABSOLUTE)
    file(TO_CMAKE_PATH "${_zf_norm}" _zf_norm)
    list(APPEND _srcs ${_zf_norm})
  endforeach()
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

  template_result("running" "compiler started")
  execute_process(
    COMMAND ${ZANC} ${_srcs} -o ${_artifact} ${_args}
    TIMEOUT "${COMPILE_TIMEOUT}"
    RESULT_VARIABLE _rc
    OUTPUT_FILE "${_out}/stdout.log"
    ERROR_FILE "${_out}/stderr.log")
  # A successful compiler exit must also produce the linked artifact. Selected
  # project directories were reset above, so an old artifact cannot pass here.
  if("${_rc}" STREQUAL "0")
    if(NOT EXISTS "${_artifact}" OR IS_DIRECTORY "${_artifact}")
      set(_rc "compiler exited 0 but artifact is missing: ${_artifact}")
    else()
      file(SIZE "${_artifact}" _artifact_size)
      if(_artifact_size EQUAL 0)
        set(_rc "compiler exited 0 but artifact is empty: ${_artifact}")
      endif()
    endif()
    if(NOT "${_rc}" STREQUAL "0")
      file(APPEND "${_out}/stderr.log" "\n${_rc}\n")
    endif()
  endif()
  # RESULT_VARIABLE can also be a launch/timeout string; preserve that text
  # in the result and accept only the explicit success code.
  if("${_rc}" STREQUAL "0")
    template_result("pass" "${_rc}")
    message(STATUS "OK: ${_rel}")
  else()
    if("${_rc}" MATCHES "[Tt]imeout")
      set(_status "timeout")
    else()
      set(_status "fail")
    endif()
    template_result("${_status}" "${_rc}")
    message(STATUS "FAIL: ${_rel} (${_rc}; logs=${_out})")
    list(APPEND _failed "${_rel}: ${_rc} (see ${_out}/stdout.log and stderr.log)")
  endif()
endforeach()

string(JSON _results SET "${_results}" complete true)
file(WRITE "${WORK}/results.json" "${_results}\n")
if(NOT _failed STREQUAL "")
  string(REPLACE ";" "\n\n" _report "${_failed}")
  message(FATAL_ERROR "template(s) failed to build:\n\n${_report}")
endif()

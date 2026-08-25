# Gate: GUI code must not gain direct Theme font-size reads.
#
# Font sizes belong to the style layer (skins/base.css + Style/StyleBox over
# Theme tokens). Widget draw and measure paths must consume the resolved
# StyleBox fontPx instead of reading Theme fontSize* fields directly.
#
# PAID OFF (298 -> 36): every widget/component draw path now consumes
# Style.FontFallback(app, size) (or a resolved StyleBox), so skins can
# override type scale from base.css. The only residual reads are the style
# layer itself (Style.zan implements the fallback funnel, Theme.zan defines
# the tokens), UiDriver.ThemeJson's palette export for driver self-checks
# (not a draw path), and two Theme-parameter-injected APIs whose callers pass
# the theme explicitly (ChartResolved.ResolvedSeries.Of, CodeEditor.FontSize).
# Widget requires zero; any file with reads that is not listed is implicit zero.
#
# Inputs: ROOT (repository root).

cmake_policy(SET CMP0007 NEW)

set(_budget
  "stdlib/Gui/Style.zan=14"
  "stdlib/Gui/Theme.zan=15"
  "stdlib/Gui/Backend/UiDriver.zan=5"
  "stdlib/Gui/Component/Chart/ChartResolved.zan=1"
  "stdlib/Gui/Component/CodeEditor/CodeEditor.zan=1"
)
set(_members "fontSizeTiny|fontSizeSmall|fontSizeMedium|fontSizeLarge|fontSizeHuge")
set(_total_budget 36)
file(GLOB_RECURSE _sources "${ROOT}/stdlib/Gui/*.zan")

set(_fail "")
set(_total 0)
set(_widget_total 0)
set(_dir_names "")
foreach(_f ${_sources})
  file(RELATIVE_PATH _rel "${ROOT}" "${_f}")
  file(STRINGS "${_f}" _lines ENCODING UTF-8)
  set(_n 0)
  foreach(_line ${_lines})
    string(STRIP "${_line}" _stripped)
    if(_stripped MATCHES "^//")
      continue()
    endif()
    string(REGEX MATCHALL
      "[^A-Za-z0-9_.](t|theme)\\.(${_members})[^A-Za-z0-9_]"
      _hits " ${_line} ")
    list(FILTER _hits EXCLUDE REGEX "^$")
    list(LENGTH _hits _c)
    math(EXPR _n "${_n} + ${_c}")
  endforeach()
  if(_n EQUAL 0)
    continue()
  endif()
  math(EXPR _total "${_total} + ${_n}")
  get_filename_component(_dir "${_rel}" DIRECTORY)
  list(APPEND _dir_names "${_dir}")
  if(_rel MATCHES "^stdlib/Gui/Widget/")
    math(EXPR _widget_total "${_widget_total} + ${_n}")
  endif()
  set(_allowed 0)
  foreach(_b ${_budget})
    if(_b MATCHES "^${_rel}=([0-9]+)$")
      set(_allowed "${CMAKE_MATCH_1}")
    endif()
  endforeach()
  if(_n GREATER _allowed)
    list(APPEND _fail
      "${_rel}: ${_n} direct Theme font-size reads (budget ${_allowed})")
  endif()
endforeach()

list(REMOVE_DUPLICATES _dir_names)
list(SORT _dir_names)
message("THEME_FONT_BUDGET total=${_total} widget=${_widget_total}")
foreach(_dir ${_dir_names})
  set(_dir_total 0)
  foreach(_f ${_sources})
    file(RELATIVE_PATH _rel "${ROOT}" "${_f}")
    get_filename_component(_file_dir "${_rel}" DIRECTORY)
    if(NOT _file_dir STREQUAL _dir)
      continue()
    endif()
    file(STRINGS "${_f}" _lines ENCODING UTF-8)
    foreach(_line ${_lines})
      string(STRIP "${_line}" _stripped)
      if(_stripped MATCHES "^//")
        continue()
      endif()
      string(REGEX MATCHALL
        "[^A-Za-z0-9_.](t|theme)\\.(${_members})[^A-Za-z0-9_]"
        _hits " ${_line} ")
      list(FILTER _hits EXCLUDE REGEX "^$")
      list(LENGTH _hits _c)
      math(EXPR _dir_total "${_dir_total} + ${_c}")
    endforeach()
  endforeach()
  message("THEME_FONT_BUDGET_DIR ${_dir}=${_dir_total}")
endforeach()

if(_fail)
  message("A GUI path gained direct Theme font-size reads. Resolve the")
  message("font through the existing StyleBox and CSS font-size rule:")
  foreach(_o ${_fail})
    message("  ${_o}")
  endforeach()
  message(FATAL_ERROR "new direct Theme font-size reads in stdlib/Gui")
endif()

if(_total GREATER _total_budget)
  message(FATAL_ERROR
    "THEME_FONT_BUDGET total=${_total} exceeds budget ${_total_budget}")
endif()

message("THEME_FONT_BUDGET_OK")

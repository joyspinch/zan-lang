# Gate: GUI code must not gain direct Theme font-size reads.
#
# Font sizes belong to the style layer (skins/base.css + Style/StyleBox over
# Theme tokens). Widget draw and measure paths must consume the resolved
# StyleBox fontPx instead of reading Theme fontSize* fields directly.
#
# The per-file budget below freezes the current residual outside Widget and
# requires zero residual reads in stdlib/Gui/Widget. A file with reads that is
# not listed has an implicit budget of zero.
#
# Inputs: ROOT (repository root).

cmake_policy(SET CMP0007 NEW)

set(_budget
  "stdlib/Gui/App.zan=2"
  "stdlib/Gui/Style.zan=14"
  "stdlib/Gui/Theme.zan=15"
  "stdlib/Gui/Backend/UiDriver.zan=5"
  "stdlib/Gui/Component/ChatView.zan=6"
  "stdlib/Gui/Component/Dock.zan=2"
  "stdlib/Gui/Component/FilePicker.zan=13"
  "stdlib/Gui/Component/LogView.zan=6"
  "stdlib/Gui/Component/PivotTable.zan=2"
  "stdlib/Gui/Component/PropertyGrid.zan=2"
  "stdlib/Gui/Component/SessionList.zan=11"
  "stdlib/Gui/Component/CefBrowser/CefBrowser.zan=1"
  "stdlib/Gui/Component/Chart/Chart.zan=9"
  "stdlib/Gui/Component/Chart/ChartBig.zan=5"
  "stdlib/Gui/Component/Chart/ChartResolved.zan=1"
  "stdlib/Gui/Component/Chart/ChartView.zan=4"
  "stdlib/Gui/Component/Chart/ChartViewBar.zan=7"
  "stdlib/Gui/Component/Chart/ChartViewEventRiver.zan=4"
  "stdlib/Gui/Component/Chart/ChartViewFinance.zan=3"
  "stdlib/Gui/Component/Chart/ChartViewHeatmap.zan=4"
  "stdlib/Gui/Component/Chart/ChartViewHier.zan=8"
  "stdlib/Gui/Component/Chart/ChartViewMap.zan=1"
  "stdlib/Gui/Component/Chart/ChartViewPie.zan=22"
  "stdlib/Gui/Component/Chart/ChartViewPolar.zan=3"
  "stdlib/Gui/Component/Chart/ChartViewRelation.zan=5"
  "stdlib/Gui/Component/Chart/ChartViewScatter.zan=3"
  "stdlib/Gui/Component/Chart/ChartViewShared.zan=5"
  "stdlib/Gui/Component/Chart/ChartViewVenn.zan=1"
  "stdlib/Gui/Component/CodeEditor/CodeEditor.Render.zan=24"
  "stdlib/Gui/Component/CodeEditor/CodeEditor.zan=2"
  "stdlib/Gui/Component/DataTable/DataTable.Overlays.zan=12"
  "stdlib/Gui/Component/DataTable/DataTable.FilterUI.zan=15"
  "stdlib/Gui/Component/WebView/WebView.zan=1"
  "stdlib/Gui/Designer/Designer.Form.zan=17"
  "stdlib/Gui/Designer/Designer.Inspector.zan=4"
  "stdlib/Gui/Designer/Designer.zan=5"
  "stdlib/Gui/Hmi/Alarm.zan=3"
  "stdlib/Gui/Hmi/Gauge.zan=3"
  "stdlib/Gui/Hmi/Indicator.zan=4"
  "stdlib/Gui/Hmi/NumPad.zan=5"
  "stdlib/Gui/Hmi/Trend.zan=1"
)
set(_members "fontSizeTiny|fontSizeSmall|fontSizeMedium|fontSizeLarge|fontSizeHuge")
set(_total_budget 234)
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

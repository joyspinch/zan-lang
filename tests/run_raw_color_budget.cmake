# Gate: widget draw paths must not gain NEW raw ARGB color literals.
#
# Colors belong to the style layer (skins/base.css + Style/StyleBox/Fx over
# Theme tokens). A raw `0xAARRGGBB` baked into a draw call hard-codes a visual
# decision the skin can never override -- the same CSS debt the semantic-color
# budget polices, just in a form that test cannot see.
#
# The existing residual is frozen in the per-file budget below; a file may only
# go DOWN. Every line carries its reason, because a raw literal is not always a
# color: Win32 style flags, hash salts and illustrative sketches all look like
# one. Files whose literals are pure native-interop flags are skipped outright.
#
# Skipped wholesale (not draw paths / not colors):
#   Backend/**                  Win32 interop (WS_*/GENERIC_*/DWM flags)
#   Component/WebView/WebView2.zan   child-window style flag combination
#   Theme.zan, Style.zan, StyleBox.zan, Fx.zan   the style layer itself
#   EditorPalette.zan           named palette definitions (colors by trade)
#
# Inputs: ROOT (repository root).

cmake_policy(SET CMP0007 NEW)

set(_budget
  "stdlib/Gui/App.zan=1"
  "stdlib/Gui/Widget/Layer.zan=2"
  "stdlib/Gui/Widget/Wizard.zan=2"
  "stdlib/Gui/Component/CodeEditor/CodeEditor.Render.zan=1"
)

set(_skip_files
  "stdlib/Gui/Component/WebView/WebView2.zan"
)

file(GLOB_RECURSE _sources "${ROOT}/stdlib/Gui/*.zan")

set(_fail "")
foreach(_f ${_sources})
  file(RELATIVE_PATH _rel "${ROOT}" "${_f}")
  if(_rel MATCHES "^stdlib/Gui/Backend/")
    continue()
  endif()
  if(_rel IN_LIST _skip_files)
    continue()
  endif()
  if(_rel STREQUAL "stdlib/Gui/Theme.zan" OR _rel STREQUAL "stdlib/Gui/Style.zan"
     OR _rel STREQUAL "stdlib/Gui/StyleBox.zan" OR _rel STREQUAL "stdlib/Gui/Fx.zan"
     OR _rel STREQUAL "stdlib/Gui/EditorPalette.zan")
    continue()
  endif()
  file(STRINGS "${_f}" _lines ENCODING UTF-8)
  set(_n 0)
  foreach(_line ${_lines})
    string(STRIP "${_line}" _stripped)
    if(_stripped MATCHES "^//")
      continue()
    endif()
    string(REGEX MATCHALL "0x[0-9A-Fa-f]{8}" _hits "${_line}")
    foreach(_h ${_hits})
      # Fully transparent / black / white are layout sentinels, not decisions.
      if(NOT _h MATCHES "0x[Ff]{6}[Ff]{2}|0x00000000|0x[Ff]{2}000000|0x000000[Ff]{2}")
        math(EXPR _n "${_n} + 1")
      endif()
    endforeach()
  endforeach()
  if(_n EQUAL 0)
    continue()
  endif()
  set(_allowed 0)
  foreach(_b ${_budget})
    if(_b MATCHES "^${_rel}=([0-9]+)$")
      set(_allowed "${CMAKE_MATCH_1}")
    endif()
  endforeach()
  if(_n GREATER _allowed)
    list(APPEND _fail "${_rel}: ${_n} raw ARGB literals (budget ${_allowed})")
  endif()
endforeach()

if(_fail)
  message("A GUI draw path gained a raw 0xAARRGGBB literal. Move the color into")
  message("skins/base.css (a part rule or :root token) and read it through the")
  message("resolved StyleBox; if the literal is genuinely not a skin color (API")
  message("flag mask, hash salt, illustrative sketch), freeze it with a budget")
  message("line here plus a code comment saying why.")
  foreach(_o ${_fail})
    message("  ${_o}")
  endforeach()
  message(FATAL_ERROR "new raw color literals in stdlib/Gui")
endif()

message("RAW_COLOR_BUDGET_OK")

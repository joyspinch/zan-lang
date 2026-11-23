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
#   Backend/**                          Win32 interop (WS_*/GENERIC_*/DWM flags)
#   Component/WebView/WebView2.zan      child-window style flag combination
#   Theme.zan, Style.zan, Style.Defaults.zan, StyleBox.zan, Fx.zan   the style layer itself
#   EditorPalette.zan, ChartSkin.zan    named palette definitions (colors by trade)
#
# Inputs: ROOT (repository root).

cmake_policy(SET CMP0007 NEW)

set(_budget
  "packages/Zan.Gui/src/Gui/Core/FocusManager.zan=1"                   # FNV-1a 32-bit hash offset basis (0x811C9DC5)
  "packages/Zan.Gui/src/Gui/Core/IdHash.zan=2"                         # FNV-1a 32-bit hash offset basis (0x811C9DC5)
  "packages/Zan.Gui/src/Gui/Core/WidgetId.zan=1"                       # FNV-1a 32-bit hash offset basis (0x811C9DC5)
  "packages/Zan.Gui/src/Gui/Rendering/Canvas.zan=1"                    # RGB 24-bit channel mask (0x00FFFFFF)
  "packages/Zan.Gui/src/Gui/Rendering/Canvas.Text.zan=2"               # RGB 24-bit channel mask (0x00FFFFFF)
  "packages/Zan.Gui/src/Gui/Text/RichTextParser.zan=6"                 # inline rich-text #R/#Y/#B/#G/#H/#L shortcut tags
  "packages/Zan.Gui.CodeEditor/src/Gui/Component/CodeEditor/CodeEditor.Highlight.zan=1" # theme signature hash salt (0x5A5A5A5A)
  "packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/Features/Sort.zan=1"          # FNV-1a hash offset basis (0x811C9DC5)
  "packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/Models/QueryDescriptor.zan=2" # FNV-1a hash offset basis (0x811C9DC5)
  "packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/Models/RowStyle.zan=8"        # RowStyle.Warning/Danger/Success/Muted model presets
)

set(_skip_files
  "packages/Zan.Gui.Browser/src/Gui/Component/WebView/WebView2.zan"
  "packages/Zan.Gui.CodeEditor/src/Gui/Component/CodeEditor/EditorPalette.zan"
  "packages/Zan.Gui.Charts/src/Gui/Component/Chart/ChartSkin.zan"
)

file(GLOB_RECURSE _sources "${ROOT}/packages/Zan.Gui*/src/Gui/*.zan")

set(_fail "")
foreach(_f ${_sources})
  file(RELATIVE_PATH _rel "${ROOT}" "${_f}")
  if(_rel MATCHES "^packages/Zan.Gui[^/]*/src/Gui/Backend/")
    continue()
  endif()
  if(_rel IN_LIST _skip_files)
    continue()
  endif()
  if(_rel STREQUAL "packages/Zan.Gui/src/Gui/Styling/Theme.zan"
     OR _rel STREQUAL "packages/Zan.Gui/src/Gui/Styling/Style/Style.zan"
     OR _rel STREQUAL "packages/Zan.Gui/src/Gui/Styling/Style/Style.Defaults.zan"
     OR _rel STREQUAL "packages/Zan.Gui/src/Gui/Styling/StyleBox/StyleBox.zan"
     OR _rel STREQUAL "packages/Zan.Gui/src/Gui/Rendering/Fx.zan")
    continue()
  endif()
  file(STRINGS "${_f}" _lines ENCODING UTF-8)
  set(_n 0)
  foreach(_line ${_lines})
    string(STRIP "${_line}" _stripped)
    if(_stripped MATCHES "^//")
      continue()
    endif()
    string(REGEX REPLACE "(^|[ \t])//.*$" "" _code "${_line}")
    string(REGEX MATCHALL "0x[0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f]([0-9A-Fa-f][0-9A-Fa-f])?" _hits "${_code}")
    foreach(_h ${_hits})
      # Fully transparent / black / white are layout sentinels, not decisions.
      if(NOT _h MATCHES "^0x([Ff][Ff][Ff][Ff][Ff][Ff]([Ff][Ff])?|00000000|000000|[Ff][Ff]000000|000000[Ff][Ff])$")
        math(EXPR _n "${_n} + 1")
      endif()
    endforeach()
  endforeach()
  if(_n EQUAL 0)
    continue()
  endif()
  set(_allowed 0)
  foreach(_b ${_budget})
    if(_b MATCHES "^([^#][^=]*)=([0-9]+)")
      set(_b_file "${CMAKE_MATCH_1}")
      set(_b_cap "${CMAKE_MATCH_2}")
      if(_rel STREQUAL "${_b_file}")
        set(_allowed "${_b_cap}")
      endif()
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
  message(FATAL_ERROR "new raw color literals in packages/Zan.Gui/src/Gui")
endif()

message("RAW_COLOR_BUDGET_OK")

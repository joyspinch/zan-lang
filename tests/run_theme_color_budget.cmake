# Gate: widget draw paths must not gain NEW direct semantic-color reads.
#
# Colors belong to the style layer (skins/base.css + Style/StyleBox/Fx over
# Theme tokens).  A widget that reads `t.primary` / `t.bgHover` while painting
# hard-codes a decision the skin can no longer override, and every such read
# is CSS debt.  The existing debt is frozen in the per-file budget below;
# a file may only go DOWN.  Exceeding its budget (or a new file appearing
# with reads and no budget line) fails this test.
#
# When you legitimately REMOVE reads, lower (or delete) that file's budget
# line so the debt cannot silently creep back.
#
# Whitelisted (they implement the style layer itself): stdlib/Gui/Theme.zan,
# Style.zan, StyleBox.zan, Fx.zan.
#
# Inputs: ROOT (repository root).

cmake_policy(SET CMP0007 NEW)

# UiDriver.ThemeJson exports the theme palette to ThemeDoc JSON for driver
# self-checks; these reads are not drawing paths. When adding exported fields,
# keep ThemeDoc, ThemeJson, and this note in sync.
set(_budget
  "stdlib/Gui/Backend/UiDriver.zan=12"
)

set(_members "primary|primaryHover|primaryPressed|info|infoHover|infoPressed|success|successHover|successPressed|warning|warningHover|warningPressed|error|errorHover|errorPressed|textPrimary|textSecondary|textTertiary|textDisabled|textInverse|bgPrimary|bgSecondary|bgTertiary|bgHover|bgActive|bgDisabled|borderPrimary|borderSecondary|borderHover|borderFocus|divider|shadowColor|scrollbar|scrollbarHover|tooltipBg|tooltipBorder|tooltipText|glassTint|glassChromeTint")

file(GLOB_RECURSE _sources "${ROOT}/stdlib/Gui/*.zan")

set(_fail "")
foreach(_f ${_sources})
  file(RELATIVE_PATH _rel "${ROOT}" "${_f}")
  if(_rel STREQUAL "stdlib/Gui/Theme.zan" OR _rel STREQUAL "stdlib/Gui/Style.zan"
     OR _rel STREQUAL "stdlib/Gui/StyleBox.zan" OR _rel STREQUAL "stdlib/Gui/Fx.zan")
    continue()
  endif()
  file(STRINGS "${_f}" _lines ENCODING UTF-8)
  set(_n 0)
  foreach(_line ${_lines})
    string(STRIP "${_line}" _stripped)
    if(_stripped MATCHES "^//")
      continue()
    endif()
    string(REGEX MATCHALL "[^A-Za-z0-9_.](t|theme)\\.(${_members})[^A-Za-z0-9_]" _hits " ${_line} ")
    # A match ending in ';' splits into an extra empty list element; drop them
    # so each read counts exactly once.
    list(FILTER _hits EXCLUDE REGEX "^$")
    list(LENGTH _hits _c)
    math(EXPR _n "${_n} + ${_c}")
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
    list(APPEND _fail "${_rel}: ${_n} direct semantic-color reads (budget ${_allowed})")
  endif()
endforeach()

if(_fail)
  message("A GUI draw path gained direct theme color reads.  Move the color")
  message("into skins/base.css (a part/state rule) or a :root token exported")
  message("by Style.RootFromTheme, and read it through the resolved StyleBox:")
  foreach(_o ${_fail})
    message("  ${_o}")
  endforeach()
  message(FATAL_ERROR "new direct semantic-color reads in stdlib/Gui")
endif()

message("THEME_COLOR_BUDGET_OK")

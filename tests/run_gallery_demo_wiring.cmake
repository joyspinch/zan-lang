# Policy: every gallery demo that is written must actually be reachable.
#
# gui_gallery.zan routes a demo to one of two renderers, and both routes have a
# way to drop a demo on the floor without any error:
#
#  1. Overlay demos (dropdowns, popovers, the data grid) are drawn in the
#     deferred overlay pass by RenderOverlay, and the base pass has to know to
#     skip them -- that is IsOverlayComp, a hand-written list of names. If a
#     name is in RenderOverlay but not in the list, the base pass falls through
#     to its final else and paints the "Overlay / interactive component"
#     placeholder, while the demo written in RenderOverlay is never called.
#     Compose and DataTable each lost a complete example this way.
#
#  2. A demo only appears if some Comp registers its preview id via AddDemo (or
#     the single-card Gallery.Add / new Comp form, whose preview id is the
#     component name). A render branch whose id nobody registers is dead code:
#     Alert.types / Alert.queue were fully implemented and never shown, so the
#     Alert page was a single placeholder card.
#
# Both failures look identical from the outside -- a page that renders, with an
# empty box on it -- which is why they survived. Compare the lists here instead.

if(NOT DEFINED ROOT)
  message(FATAL_ERROR "ROOT not set")
endif()

set(_src "${ROOT}/examples/gui_gallery/gui_gallery.zan")
if(NOT EXISTS "${_src}")
  message(FATAL_ERROR "missing ${_src}")
endif()
# Read line-by-line WITHOUT file(STRINGS) / per-line CMake lists: `;` is the
# CMake list separator, so any source line containing one (statement
# terminators, or demo strings like "Pixel scroll; rows ...") fuses with or
# splits against its neighbours once it passes through a list value, and the
# "registered preview id" / "render branch" regex scans below miss lines that
# actually exist -- that is how Menu.expand / Pagination / Scrollbar were
# flagged as dead code while every AddDemo/Gallery.Add was present.  (The
# usual `\;` escape is not available either: `${x}` expansion of a `;` re-splits
# at the argument level, so no quoted argument can carry the two-character
# escape into string()).
#
# Approach: map every `;` in the file to a SOH control char (never present in
# source) and every newline to `|` (also never present), then slice physical
# lines with a while loop and scan each one immediately -- the line only ever
# lives in a scalar variable, never in a list.  Every regex below is free of
# literal `;`, so matching against the SOH-protected line is exact; only the
# captured ids (whose char classes exclude `;`) are appended to the
# bookkeeping lists.
file(READ "${_src}" _content)
string(ASCII 1 _soh)
string(ASCII 124 _pipe)
string(REPLACE ";" "${_soh}" _content "${_content}")
string(REPLACE "\r\n" "\n" _content "${_content}")
string(REPLACE "\r" "\n" _content "${_content}")
string(REPLACE "\n" "${_pipe}" _content "${_content}")

# Split the file into the three regions we care about by their function headers.
set(_region "")
set(_overlay_branch "")
set(_overlay_listed "")
set(_all_branch "")
set(_registered "")
set(_rem "${_content}")
string(LENGTH "${_rem}" _rem_len)
while(_rem_len GREATER 0)
  string(FIND "${_rem}" "${_pipe}" _pos)
  if(_pos EQUAL -1)
    set(_l "${_rem}")
    set(_rem_len 0)
  else()
    string(SUBSTRING "${_rem}" 0 ${_pos} _l)
    math(EXPR _next "${_pos}+1")
    string(SUBSTRING "${_rem}" ${_next} -1 _rem)
    string(LENGTH "${_rem}" _rem_len)
  endif()

  if(_l MATCHES "^    static bool IsOverlayComp\\(")
    set(_region "list")
  elseif(_l MATCHES "^    static void RenderOverlay\\(")
    set(_region "overlay")
  elseif(_l MATCHES "^    static (int|void|bool|string|Comp|Control|Panel|Flex) ")
    if(_region STREQUAL "list" OR _region STREQUAL "overlay")
      set(_region "")
    endif()
  endif()

  # Registered preview ids, from anywhere in the file.
  if(_l MATCHES "\"([A-Za-z0-9_. ]+)\",[ \t]*[0-9]+,")
    list(APPEND _registered "${CMAKE_MATCH_1}")
  endif()
  if(_l MATCHES "Gallery\\.Add(Json)?\\(list,[ \t]*\"[^\"]*\",[ \t]*\"([A-Za-z0-9_. ]+)\"")
    list(APPEND _registered "${CMAKE_MATCH_2}")
  endif()
  if(_l MATCHES "new Comp\\(\"[^\"]*\",[ \t]*\"([A-Za-z0-9_. ]+)\"")
    list(APPEND _registered "${CMAKE_MATCH_1}")
  endif()

  if(_l MATCHES "name == \"([A-Za-z0-9_. ]+)\"")
    set(_hit "${CMAKE_MATCH_1}")
    list(APPEND _all_branch "${_hit}")
    if(_region STREQUAL "overlay")
      list(APPEND _overlay_branch "${_hit}")
    elseif(_region STREQUAL "list")
      list(APPEND _overlay_listed "${_hit}")
    endif()
  endif()
endwhile()

foreach(_v _overlay_branch _overlay_listed _all_branch _registered)
  if(${_v})
    list(REMOVE_DUPLICATES ${_v})
    list(SORT ${_v})
  endif()
endforeach()

if(NOT _overlay_branch)
  message(FATAL_ERROR "parsed no RenderOverlay branches -- the check is broken, "
                      "not the gallery")
endif()

set(_bad 0)

# 1. RenderOverlay branch that IsOverlayComp does not route to it.
foreach(_n IN LISTS _overlay_branch)
  if(NOT "${_n}" IN_LIST _overlay_listed)
    message(SEND_ERROR
      "gui_gallery.zan: RenderOverlay draws \"${_n}\" but IsOverlayComp does "
      "not list it, so the base pass paints the placeholder instead and that "
      "demo never runs. Add `if (name == \"${_n}\") { return true; }`.")
    set(_bad 1)
  endif()
endforeach()

# 2. IsOverlayComp entry with nothing in RenderOverlay to draw it.
foreach(_n IN LISTS _overlay_listed)
  if(NOT "${_n}" IN_LIST _overlay_branch)
    message(SEND_ERROR
      "gui_gallery.zan: IsOverlayComp lists \"${_n}\" but RenderOverlay has no "
      "branch for it, so its preview box is left empty.")
    set(_bad 1)
  endif()
endforeach()

# 3. Render branch whose preview id nobody registers.
foreach(_n IN LISTS _all_branch)
  if(NOT "${_n}" IN_LIST _registered)
    message(SEND_ERROR
      "gui_gallery.zan: a render branch draws \"${_n}\" but no Comp registers "
      "that preview id, so the demo is dead code and never appears. Register "
      "it with AddDemo(title, desc, \"${_n}\", h, code, json).")
    set(_bad 1)
  endif()
endforeach()

if(_bad)
  message(FATAL_ERROR "gallery demo wiring check failed")
endif()
list(LENGTH _all_branch _n_branch)
message(STATUS "gallery demo wiring OK (${_n_branch} demos reachable)")

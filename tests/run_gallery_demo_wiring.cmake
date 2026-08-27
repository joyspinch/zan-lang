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
file(STRINGS "${_src}" _lines)

# Split the file into the three regions we care about by their function headers.
set(_region "")
set(_overlay_branch "")
set(_overlay_listed "")
set(_all_branch "")
set(_registered "")
foreach(_l IN LISTS _lines)
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
endforeach()

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

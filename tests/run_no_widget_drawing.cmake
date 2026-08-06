# Guard: GUI examples and templates must not draw widget internals themselves.
#
# The rule is that a control owns its own appearance. An example may lay out its
# own chrome (panels, headings, its own table of rows -- plain rects and text),
# but the moment it reaches for the primitives a gauge / trend / bargraph / pie
# is made of, the same widget exists in two places and they drift: the component
# gets fixed and the demo keeps showing the old, wrong drawing. That is exactly
# how the gallery's gauge demo ended up not matching the HMI gauge.
#
# So the primitives listed below -- circular/arc geometry, which nothing but a
# dial, ring or pie needs -- are banned outside stdlib. Games are exempt: they
# render their own scene by definition and never stand in for a control.
#
# Inputs: ROOT (repository root).

set(_banned "FillSector" "DrawSector" "FillArc" "DrawArc" "FillPie" "DrawPie")

file(GLOB_RECURSE _sources "${ROOT}/examples/*.zan" "${ROOT}/templates/*.zan")

set(_offences "")
foreach(_f ${_sources})
  # Games draw their own scene; they are not standing in for a control.
  if(_f MATCHES "/examples/game/")
    continue()
  endif()
  file(STRINGS "${_f}" _lines)
  set(_n 0)
  foreach(_line ${_lines})
    math(EXPR _n "${_n} + 1")
    foreach(_bad ${_banned})
      if(_line MATCHES "\\.${_bad}\\(")
        file(RELATIVE_PATH _rel "${ROOT}" "${_f}")
        string(STRIP "${_line}" _stripped)
        list(APPEND _offences "${_rel}:${_n}: ${_stripped}")
      endif()
    endforeach()
  endforeach()
endforeach()

if(_offences)
  message("A GUI example or template draws widget internals itself.")
  message("Move the drawing into the component (stdlib/Gui/...) and let the")
  message("example only instantiate it, configure it and feed it data:")
  foreach(_o ${_offences})
    message("  ${_o}")
  endforeach()
  message(FATAL_ERROR "widget drawing found outside stdlib")
endif()

message("NO_WIDGET_DRAWING_OK scanned=${_sources}")

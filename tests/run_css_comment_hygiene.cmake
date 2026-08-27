# Policy: a CSS comment must not contain `*/` in its body.
#
# CSS comments do not nest: the first `*/` closes the comment, exactly as every
# real CSS engine does. So writing a token like `--*-ghost-*/--*-soft-*` inside
# a comment ends it early, and everything the author meant as prose becomes
# stylesheet content -- silently gluing itself to the first selector of the
# next rule. That is how `input` lost its entire base rule (no border, no field
# background) in base.css while `textarea`/`select` from the same selector list
# kept theirs, with nothing anywhere reporting an error.
#
# The tell is precise: once a comment closes early, the closer the author did
# write is left behind as content. So strip comments the way a parser does and
# fail if a stray `*/` survives.

if(NOT DEFINED ROOT)
  message(FATAL_ERROR "ROOT not set")
endif()

file(GLOB_RECURSE _css "${ROOT}/stdlib/*.css" "${ROOT}/templates/*.css"
                       "${ROOT}/examples/*.css" "${ROOT}/src/*.css")

set(_bad 0)
foreach(_f IN LISTS _css)
  file(READ "${_f}" _txt)
  # Strip comments exactly like the parser: from each `/*` to the first `*/`.
  set(_out "")
  set(_rest "${_txt}")
  while(TRUE)
    string(FIND "${_rest}" "/*" _open)
    if(_open LESS 0)
      string(APPEND _out "${_rest}")
      break()
    endif()
    string(SUBSTRING "${_rest}" 0 ${_open} _head)
    string(APPEND _out "${_head}")
    math(EXPR _after "${_open} + 2")
    string(SUBSTRING "${_rest}" ${_after} -1 _tail)
    string(FIND "${_tail}" "*/" _close)
    if(_close LESS 0)
      # Unterminated comment: the whole tail is comment. Report it too.
      set(_rest "")
      message(SEND_ERROR "${_f}: unterminated /* comment")
      set(_bad 1)
      break()
    endif()
    math(EXPR _resume "${_close} + 2")
    string(SUBSTRING "${_tail}" ${_resume} -1 _rest)
  endwhile()

  string(FIND "${_out}" "*/" _stray)
  if(_stray GREATER_EQUAL 0)
    # Show the line the leftover closer sits on, and the prose around it.
    string(SUBSTRING "${_out}" 0 ${_stray} _before)
    string(REGEX REPLACE "[^\n]" "" _nl "${_before}")
    string(LENGTH "${_nl}" _lines)
    math(EXPR _lineno "${_lines} + 1")
    string(SUBSTRING "${_out}" ${_stray} 60 _ctx)
    string(REGEX REPLACE "[\r\n]+" " " _ctx "${_ctx}")
    message(SEND_ERROR
      "${_f}: a comment body contains `*/`, so the comment ends early and the "
      "rest of it leaks into the stylesheet (swallowing the next rule's first "
      "selector). Leftover closer near stripped-content line ${_lineno}: ${_ctx}")
    set(_bad 1)
  endif()
endforeach()

if(_bad)
  message(FATAL_ERROR "CSS comment hygiene check failed")
endif()
message(STATUS "css comment hygiene OK")

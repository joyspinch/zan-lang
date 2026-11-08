# Repo-wide compile gate: gen1 must compile the whole repository's Zan sources,
# not just the 230 hand-picked differential cases.
#
# The differential gate proves gen1 agrees with the C host on curated cases.
# It says nothing about the far larger surface the repo actually ships: the
# stdlib, every package, the IDE project templates and the examples. This gate
# closes that hole: for every unit (a directory whose .zan files are compiled
# as one invocation), gen1 must exit 0 and write the .ll -- fail-closed, the
# same boundary the differential gate asserts.
#
# A unit is a directory: all .zan files under it (recursively) are passed to
# gen1 in one invocation, mirroring how the C host compiles a program or a
# package. The special tier root "stdlib" is one unit (every stdlib file,
# mirroring the demand-pull closure a real program sees); every other tier
# root contributes one unit per first-level project directory, discovered at
# gate time so a newly added package is covered without touching a list.
#
# Units gen1 cannot lower yet are listed in the known-gaps file (reported, not
# failed) -- they are the recorded repo-gate gaps, and a gap that starts
# passing is reported as stale so the list gets pruned. The gate only fails on
# a unit that is neither listed as a gap nor passing.
#
# Invoked as:
#   cmake -DZANC=<zanc> -DSELFHOST_SOURCES=<list> -DGEN1=<exe> -DOUTDIR=<dir> \
#         -DTIER_ROOTS=stdlib,examples,templates[,packages] -DGAPS=<gaps.txt> \
#         [-DWORKDIR=<dir>] -P run_selfhost_repo.cmake

cmake_policy(SET CMP0012 NEW)

if(NOT GEN1 OR NOT OUTDIR OR NOT TIER_ROOTS)
  message(FATAL_ERROR "run_selfhost_repo.cmake: GEN1, OUTDIR and TIER_ROOTS are required")
endif()
if(NOT SKIP_GEN1_BUILD AND NOT ZANC)
  message(FATAL_ERROR "run_selfhost_repo.cmake: ZANC is required unless SKIP_GEN1_BUILD=1")
endif()

# The gate runs from the repo root: gen1 resolves its stdlib/package pulls
# relative to the working directory, the way the differential gate does.
if(NOT WORKDIR)
  get_filename_component(_script_dir "${CMAKE_SCRIPT_MODE_FILE}" DIRECTORY)
  get_filename_component(WORKDIR "${_script_dir}/.." ABSOLUTE)
endif()

file(MAKE_DIRECTORY ${OUTDIR})

# ---- build gen1 once -------------------------------------------------------
# SKIP_GEN1_BUILD=1 reuses a caller-provided GEN1 (validation flows that must
# run the gate against a gen1 built from a different tree, e.g. a HEAD
# worktree while an in-flight refactor leaves the live tree's compiler
# non-viable). ctest never sets it -- the gate always rebuilds from the tree.
set(_srcs ${SELFHOST_SOURCES})
if(NOT _srcs AND NOT SKIP_GEN1_BUILD)
  message(FATAL_ERROR "run_selfhost_repo.cmake: SELFHOST_SOURCES is required (scripts/selfhost_sources.txt)")
endif()
if(NOT SKIP_GEN1_BUILD)
  list(GET _srcs 0 _first)
  list(REMOVE_AT _srcs 0)
  execute_process(
    COMMAND ${ZANC} ${_first} ${_srcs} --auto-stdlib -o ${GEN1}
    RESULT_VARIABLE build_rc
    OUTPUT_VARIABLE build_out
    ERROR_VARIABLE build_err)
  if(NOT build_rc EQUAL 0)
    message(FATAL_ERROR "gen1 build failed (rc=${build_rc})\n${build_out}${build_err}")
  endif()
endif()

# ---- known gaps ------------------------------------------------------------
set(_gaps)
if(GAPS)
  if(NOT EXISTS ${GAPS})
    message(FATAL_ERROR "run_selfhost_repo.cmake: the known-gaps file '${GAPS}' does not exist (pass an absolute path)")
  endif()
  file(STRINGS ${GAPS} _gap_lines)
  foreach(_g IN LISTS _gap_lines)
    string(STRIP "${_g}" _g)
    if(_g STREQUAL "" OR _g MATCHES "^#")
      continue()
    endif()
    # "unit -- reason": the reason is documentation, the name is the key
    string(FIND "${_g}" " --" _sep)
    if(NOT _sep EQUAL -1)
      string(SUBSTRING "${_g}" 0 ${_sep} _g)
      string(STRIP "${_g}" _g)
    endif()
    list(APPEND _gaps "${_g}")
  endforeach()
endif()

set(_failures 0)
set(_gap_hits 0)
set(_passed 0)
set(_skipped 0)

# ---- the unit loop ---------------------------------------------------------
# One unit per first-level project directory under each tier root (stdlib is
# a single unit). Directories without .zan files are skipped (asset dirs).
string(REPLACE "," ";" _tier_roots "${TIER_ROOTS}")
foreach(_tier IN LISTS _tier_roots)
  if(NOT EXISTS "${WORKDIR}/${_tier}")
    math(EXPR _failures "${_failures} + 1")
    message(SEND_ERROR "FAIL tier root '${_tier}': does not exist under ${WORKDIR}")
    continue()
  endif()
  set(_unit_dirs "")
  if(_tier STREQUAL "stdlib")
    set(_unit_dirs "${_tier}")
  elseif(_tier STREQUAL "packages")
    # one unit per package root: a package's sources cross-reference each
    # other across subdirectories, splitting it would invent resolution
    # errors the C host never sees
    file(GLOB _pkg_dirs RELATIVE "${WORKDIR}" "${WORKDIR}/${_tier}/*")
    list(APPEND _unit_dirs ${_pkg_dirs})
  else()
    # project tier (examples/templates): unit = the first directory level
    # that DIRECTLY contains .zan sources. Template categories nest one level
    # deeper than example projects, and each template project is an
    # independent program with its own Main -- compiling a whole category dir
    # recursively would merge unrelated Mains into one compilation.
    file(GLOB_RECURSE _children LIST_DIRECTORIES TRUE "${WORKDIR}/${_tier}/*")
    # Top-down pruning: GLOB_RECURSE returns entries sorted with parents
    # before their descendants, so a dir that becomes a unit prunes its whole
    # subtree -- deeper dirs with .zan sources are part of that unit's
    # compilation (gen1 collects a unit directory recursively), never units
    # of their own.
    set(_pruned "")
    foreach(_d IN LISTS _children)
      # GLOB_RECURSE with an absolute pattern returns absolute paths
      if(NOT IS_DIRECTORY "${_d}")
        continue()
      endif()
      set(_covered 0)
      foreach(_p IN LISTS _pruned)
        string(FIND "${_d}/" "${_p}/" _pre)
        if(_pre EQUAL 0)
          set(_covered 1)
          break()
        endif()
      endforeach()
      if(_covered EQUAL 1)
        continue()
      endif()
      file(GLOB _direct "${_d}/*.zan")
      if(NOT _direct)
        continue()
      endif()
      # IDE project templates are TEXT with {{NAME}} substitution markers --
      # they only become compilable sources after the IDE instantiates them,
      # so a raw template unit is not a compile gate subject. Skip it (not a
      # pass, a gap or a failure: the unit is simply out of scope).
      set(_ph 0)
      file(GLOB_RECURSE _all_files "${_d}/*.zan")
      foreach(_f IN LISTS _all_files)
        file(STRINGS "${_f}" _phm REGEX "\\{\\{" LIMIT_COUNT 1)
        if(_phm)
          set(_ph 1)
          break()
        endif()
      endforeach()
      if(_ph EQUAL 1)
        math(EXPR _skipped "${_skipped} + 1")
        file(RELATIVE_PATH _rel "${WORKDIR}" "${_d}")
        message(STATUS "SKIP ${_rel} (IDE template with {{NAME}} placeholders)")
        list(APPEND _pruned "${_d}")
        continue()
      endif()
      file(RELATIVE_PATH _rel "${WORKDIR}" "${_d}")
      list(APPEND _unit_dirs "${_rel}")
      list(APPEND _pruned "${_d}")
    endforeach()
  endif()
  foreach(_dir IN LISTS _unit_dirs)
    # gen1 accepts a directory argument directly (recursive *.zan collection,
    # mirroring the C host CLI) -- a unit with thousands of files must not go
    # through argv, Windows command-line limits cap that around 32k chars.
    file(GLOB_RECURSE _files "${WORKDIR}/${_dir}/*.zan")
    list(LENGTH _files _n)
    if(_n EQUAL 0)
      continue()
    endif()
    string(REPLACE "/" "_" _slug "${_dir}")

    set(_ll "${OUTDIR}/${_slug}.ll")
    file(REMOVE ${_ll})

    set(_why "")
    execute_process(
      COMMAND ${GEN1} ${_ll} "${WORKDIR}/${_dir}"
      WORKING_DIRECTORY ${WORKDIR}
      RESULT_VARIABLE gen_rc
      OUTPUT_VARIABLE gen_out
      ERROR_VARIABLE gen_err
      TIMEOUT 3600)
    if(NOT gen_rc EQUAL 0)
      string(SUBSTRING "${gen_out}${gen_err}" 0 400 _why)
      set(_why "gen1 failed to compile it (rc=${gen_rc}): ${_why}")
    elseif(NOT EXISTS ${_ll})
      set(_why "gen1 exited 0 but wrote no IR (fail-closed boundary)")
    endif()

    if(_why STREQUAL "")
      math(EXPR _passed "${_passed} + 1")
      message(STATUS "PASS ${_dir} (${_n} files)")
      if("${_dir}" IN_LIST _gaps)
        # the gap is closed but the list still names it: report it so the list
        # gets pruned (a warning, not a failure -- the behaviour is correct now)
        message(WARNING "known gap '${_dir}' now passes: delete it from ${GAPS}")
      endif()
    elseif("${_dir}" IN_LIST _gaps)
      math(EXPR _gap_hits "${_gap_hits} + 1")
      message(STATUS "GAP  ${_dir} (${_n} files): ${_why}")
    else()
      math(EXPR _failures "${_failures} + 1")
      message(SEND_ERROR "FAIL ${_dir} (${_n} files): ${_why}")
    endif()
  endforeach()
endforeach()

message(STATUS "repo gate: ${_passed} passed, ${_gap_hits} known gaps, ${_skipped} skipped (templates), ${_failures} failures")
if(NOT _failures EQUAL 0)
  message(FATAL_ERROR "selfhost repo gate: ${_failures} unit(s) failed")
endif()

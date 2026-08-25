cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ROOT OR NOT DEFINED BINARY_DIR OR NOT DEFINED ZANIDE)
  message(FATAL_ERROR "run_frame_budget.cmake requires ROOT, BINARY_DIR, and ZANIDE")
endif()

if(NOT EXISTS "${ZANIDE}")
  message("FRAME_BUDGET_SKIPPED: ZanIDE executable does not exist: ${ZANIDE}")
  return()
endif()

# Damage-clipped frames are opt-in (App.SetPartialFrames, driven by the IDE's own
# profile key below): correctness first, since a single under-declared damage
# rect leaves last frame's pixels on screen. This budget exists to measure that
# optimization, so it turns it on; -DPARTIAL_FRAMES=0 measures the whole-frame
# path instead (the shipped default, whose cost the caching layers must carry).
if(NOT DEFINED PARTIAL_FRAMES)
  set(PARTIAL_FRAMES 1)
endif()

# Which renderer the measured run uses (the IDE's own `renderBackend` profile
# key): 0 CPU, 1 GL, 2 auto. The two backends do not share a budget -- the GPU
# path never touches the CPU rasterizer, so its pixel counters read near zero
# while its render_ms measures upload, batch and swap instead -- so each one
# reads its own budget file and is calibrated on its own.
if(NOT DEFINED RENDER_BACKEND)
  set(RENDER_BACKEND 0)
endif()
if(NOT DEFINED BUDGET_FILE)
  if(RENDER_BACKEND EQUAL 0)
    set(BUDGET_FILE "${ROOT}/tests/perf/frame_budget.txt")
  else()
    set(BUDGET_FILE "${ROOT}/tests/perf/frame_budget_gl.txt")
  endif()
endif()
# -DCALIBRATE=1 prints the measured numbers in budget-file syntax and asserts
# nothing: that is how a budget file is produced on a machine whose GPU (or
# CPU) has never been measured before.
if(NOT DEFINED CALIBRATE)
  set(CALIBRATE 0)
endif()

set(_run "${BINARY_DIR}/frame_budget_run")
file(REMOVE_RECURSE "${_run}")
file(MAKE_DIRECTORY "${_run}")
file(MAKE_DIRECTORY "${_run}/uidrv")

file(TO_CMAKE_PATH "${ROOT}/tests/perf/ide_hover_scroll.uidrv" _script)
file(TO_CMAKE_PATH "${_run}/uidrv" _uidrv_out)

set(_profile "${_run}/profile")

# The opened project is part of the calibration surface too: the driver script
# hovers and scrolls the explorer, so how many rows it has decides how many
# frames the run even produces (against a 6-entry tree the scrolls are no-ops
# and the profiler never fills its four 30-frame batches), and so does the file
# count, because project sources are read in per-frame slices. Generate a fixed
# project instead of opening the repo or whatever the developer had open --
# repo contents drift, and a developer's project is not reproducible at all.
set(_project "${_run}/fixture_project")
file(WRITE "${_project}/zan.proj"
  "name = FrameBudget\ntype = console\nentry = src/main.zan\ntarget = exe\nplatform = windows-x64\n")
set(_main "")
foreach(_i RANGE 1 60)
  string(APPEND _main
    "/// Frame-budget fixture line block ${_i}.\n"
    "int budgetHelper${_i}(int seed) {\n"
    "    int acc = seed;\n"
    "    for (int i = 0; i < ${_i}; i = i + 1) { acc = acc + i * ${_i}; }\n"
    "    return acc;\n"
    "}\n\n")
endforeach()
file(WRITE "${_project}/src/main.zan" "${_main}void main() {\n    Console.WriteLine(\"frame budget fixture\");\n}\n")
file(WRITE "${_project}/README.md" "# Frame budget fixture\n")
foreach(_m RANGE 1 20)
  foreach(_f a b c d e f g h i j)
    file(WRITE "${_project}/mod${_m}/${_f}.zan"
      "class Mod${_m}${_f} {\n    static int Value() { return ${_m}; }\n}\n")
  endforeach()
endforeach()
file(TO_CMAKE_PATH "${_project}" _project_cmake)

# ZanIDE reads its skin and panel widths from its config directory. Since the
# config moved beside the executable (UserConfigDir = ExeDir/config) the old
# %APPDATA% location is only a migration SOURCE, never read while the exe-side
# config exists -- so a fixture written there is silently ignored and the gate
# ends up measuring whatever settings the developer's own build carries (this
# exact mismatch once measured whole-window frames at the wrong skin and
# failed every pixel budget). ZAN_IDE_CONFIG_DIR pins the instance to the run
# dir: the values here ARE the calibration surface, the developer's own
# settings are never read or written.
file(WRITE "${_profile}/config.cfg"
  "treeW=240;bottomH=190;winW=1294;winH=857;autoRun=0;skin=emerald;rightW=600;opacity=100;wallOpacity=100;partialFrames=${PARTIAL_FRAMES};renderBackend=${RENDER_BACKEND};wallpaper=;")
# Without this the IDE scaffolds a throwaway sample project under the user's
# home on first launch.
file(WRITE "${_profile}/lastproject.txt" "${_project_cmake}")
file(TO_NATIVE_PATH "${_profile}" _profile_native)

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "ZAN_FRAME_PROF=1"
    "ZAN_UI_SCRIPT=${_script}"
    "ZAN_UI_OUT=${_uidrv_out}"
    "ZAN_IDE_CONFIG_DIR=${_profile_native}"
    "APPDATA=${_profile_native}"
    "XDG_CONFIG_HOME=${_profile_native}"
    "${ZANIDE}"
  WORKING_DIRECTORY "${_run}"
  TIMEOUT 180
  RESULT_VARIABLE _run_rc
  OUTPUT_FILE "${_run}/stdout.log"
  ERROR_FILE "${_run}/stderr.log")

set(_pixel_failures "")
foreach(_snapshot after-scroll final)
  set(_a_snapshot "${_run}/uidrv/${_snapshot}-a.bin")
  set(_b_snapshot "${_run}/uidrv/${_snapshot}-b.bin")
  set(_c_snapshot "${_run}/uidrv/${_snapshot}-c.bin")
  set(_a_size "${_a_snapshot}.size")
  set(_b_size "${_b_snapshot}.size")
  set(_c_size "${_c_snapshot}.size")
  foreach(_required_file "${_a_snapshot}" "${_b_snapshot}" "${_c_snapshot}"
                          "${_a_size}" "${_b_size}" "${_c_size}")
    if(NOT EXISTS "${_required_file}")
      message(FATAL_ERROR
        "PIXEL_COMPARE_MISSING: ${_snapshot} A/B/C snapshot ${_required_file}")
    endif()
  endforeach()
  file(READ "${_a_size}" _a_dims)
  file(READ "${_b_size}" _b_dims)
  file(READ "${_c_size}" _c_dims)
  string(STRIP "${_a_dims}" _a_dims)
  string(STRIP "${_b_dims}" _b_dims)
  string(STRIP "${_c_dims}" _c_dims)
  if(NOT _a_dims STREQUAL _b_dims OR NOT _b_dims STREQUAL _c_dims)
    message(FATAL_ERROR
      "PIXEL_COMPARE_FAILED: ${_snapshot} A/B/C surfaces "
      "${_a_dims} / ${_b_dims} / ${_c_dims}")
  endif()
  string(REGEX MATCH "^([0-9]+)x([0-9]+)$" _dims_match "${_a_dims}")
  if(NOT _dims_match)
    message(FATAL_ERROR
      "PIXEL_COMPARE_FAILED: ${_snapshot} invalid dimensions ${_a_dims}")
  endif()
  set(_pixel_width "${CMAKE_MATCH_1}")
  set(_pixel_height "${CMAKE_MATCH_2}")
  math(EXPR _expected_bytes "24 + ${_pixel_width} * ${_pixel_height} * 4")
  foreach(_label A B C)
    set(_path "${_run}/uidrv/${_snapshot}-${_label}.bin")
    file(SIZE "${_path}" _bytes)
    if(NOT _bytes EQUAL _expected_bytes)
      message(FATAL_ERROR
        "PIXEL_COMPARE_FAILED: ${_snapshot}-${_label}"
        " expected_bytes=${_expected_bytes} actual_bytes=${_bytes}")
    endif()
  endforeach()
  set(_bbox_script "${_run}/pixel_bbox.ps1")
  if(NOT EXISTS "${_bbox_script}")
    file(WRITE "${_bbox_script}" [=[
param([string]$PathA, [string]$PathB, [int]$Width, [int]$Height)
$a = [IO.File]::ReadAllBytes($PathA)
$b = [IO.File]::ReadAllBytes($PathB)
$first = -1
$minX = $Width
$minY = $Height
$maxX = -1
$maxY = -1
$diffPixels = 0
$pixels = [math]::Floor(($a.Length - 24) / 4)
for ($pixel = 0; $pixel -lt $pixels; $pixel++) {
  $i = 24 + $pixel * 4
  $changed = $false
  for ($channel = 0; $channel -lt 4; $channel++) {
    if ($a[$i + $channel] -ne $b[$i + $channel]) {
      $changed = $true
      if ($first -lt 0) { $first = $i + $channel }
    }
  }
  if ($changed) {
    $diffPixels++
    $x = $pixel % $Width
    $y = [math]::Floor($pixel / $Width)
    if ($x -lt $minX) { $minX = $x }
    if ($y -lt $minY) { $minY = $y }
    if ($x -gt $maxX) { $maxX = $x }
    if ($y -gt $maxY) { $maxY = $y }
  }
}
Write-Output "$first;$diffPixels;$minX;$minY;$maxX;$maxY"
]=])
  endif()
  foreach(_comparison AB BC)
    if(_comparison STREQUAL "AB")
      set(_left "${_a_snapshot}")
      set(_right "${_b_snapshot}")
      set(_pair "A_vs_B")
    else()
      set(_left "${_b_snapshot}")
      set(_right "${_c_snapshot}")
      set(_pair "B_vs_C")
    endif()
    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E compare_files "${_left}" "${_right}"
      RESULT_VARIABLE _pixel_cmp_rc)
    if(_pixel_cmp_rc EQUAL 0)
      message("PIXEL_COMPARE_OK: ${_snapshot} ${_pair}"
        " bytes=${_expected_bytes} surface=${_a_dims}")
    else()
      execute_process(
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass
          -File "${_bbox_script}" "${_left}" "${_right}"
          "${_pixel_width}" "${_pixel_height}"
        RESULT_VARIABLE _bbox_rc
        OUTPUT_VARIABLE _bbox
        OUTPUT_STRIP_TRAILING_WHITESPACE)
      if(NOT _bbox_rc EQUAL 0)
        message(FATAL_ERROR
          "PIXEL_COMPARE_FAILED: ${_snapshot} ${_pair}"
          " could not calculate diff bbox rc=${_bbox_rc}")
      endif()
      list(GET _bbox 0 _diff_byte)
      list(GET _bbox 1 _diff_pixels)
      list(GET _bbox 2 _diff_x_min)
      list(GET _bbox 3 _diff_y_min)
      list(GET _bbox 4 _diff_x_max)
      list(GET _bbox 5 _diff_y_max)
      math(EXPR _pixel_index "(${_diff_byte} - 24) / 4")
      math(EXPR _diff_x "${_pixel_index} % ${_pixel_width}")
      math(EXPR _diff_y "${_pixel_index} / ${_pixel_width}")
      set(_panel "unknown")
      set(_in_explorer FALSE)
      set(_in_editor FALSE)
      if(_diff_x_min LESS 390 AND _diff_y_max GREATER_EQUAL 189
         AND _diff_y_min LESS_EQUAL 1205)
        set(_in_explorer TRUE)
      endif()
      if(_diff_x_max GREATER_EQUAL 390 AND _diff_y_max GREATER_EQUAL 189
         AND _diff_y_min LESS_EQUAL 1205)
        set(_in_editor TRUE)
      endif()
      if(_in_explorer AND _in_editor)
        set(_panel "multiple")
      elseif(_in_explorer)
        set(_panel "explorer")
      elseif(_in_editor)
        set(_panel "editor")
      endif()
      set(_failure
        "${_snapshot} ${_pair} first_diff_byte=${_diff_byte}"
        "pixel=${_diff_x},${_diff_y} diff_pixels=${_diff_pixels}"
        "diff_bbox=(${_diff_x_min},${_diff_y_min})-(${_diff_x_max},${_diff_y_max})"
        "panel=${_panel} surface=${_a_dims}")
      list(JOIN _failure " " _failure_line)
      list(APPEND _pixel_failures "${_failure_line}")
      message(SEND_ERROR "PIXEL_COMPARE_FAILED: ${_failure_line}")
    endif()
  endforeach()
endforeach()

if(_run_rc MATCHES "timeout")
  execute_process(
    COMMAND taskkill /F /T /IM ZanIDE.exe
    OUTPUT_QUIET ERROR_QUIET
    RESULT_VARIABLE _kill_rc)
  message(FATAL_ERROR "ZanIDE timed out after 180 seconds; residual cleanup rc=${_kill_rc}")
endif()

if(NOT EXISTS "${_run}/uidrv/results.log")
  message(FATAL_ERROR "UiDriver results.log was not produced (rc=${_run_rc})")
endif()
file(READ "${_run}/uidrv/results.log" _results)
string(REPLACE "\r\n" "\n" _results "${_results}")
if(NOT _results MATCHES "quit")
  message(FATAL_ERROR "UiDriver results.log does not contain quit (rc=${_run_rc})")
endif()
if(NOT _results MATCHES "DONE")
  message(FATAL_ERROR "UiDriver results.log does not contain DONE (rc=${_run_rc})")
endif()
if(NOT _run_rc EQUAL 0)
  message(FATAL_ERROR "ZanIDE exited with rc=${_run_rc} after UiDriver completion")
endif()

if(NOT EXISTS "${_run}/zan_frame_perf.log")
  message(FATAL_ERROR "zan_frame_perf.log was not produced (rc=${_run_rc})")
endif()
file(READ "${_run}/zan_frame_perf.log" _log)
string(REPLACE "\r\n" "\n" _log "${_log}")
string(REPLACE "\n" ";" _lines "${_log}")

set(_first_count 0)
set(_frame_count 0)
set(_frame_indexes "")
list(LENGTH _lines _line_count)
math(EXPR _last_line "${_line_count} - 1")
foreach(_i RANGE 0 ${_last_line})
  list(GET _lines ${_i} _line)
  if(_line MATCHES "^firstframe ")
    math(EXPR _first_count "${_first_count} + 1")
    set(_first_line "${_line}")
  endif()
  if(_line MATCHES "^frames=30 ")
    math(EXPR _frame_count "${_frame_count} + 1")
    list(APPEND _frame_indexes "${_i}")
  endif()
endforeach()

if(_frame_count LESS 4)
  message(FATAL_ERROR "frame log has only ${_frame_count} frames=30 batches; need at least 4")
endif()
if(NOT _first_count EQUAL 1)
  message(FATAL_ERROR "frame log has ${_first_count} firstframe lines; need exactly 1")
endif()

if(CALIBRATE)
  # Nothing to compare against: the run below only reports what it measured.
  set(_calibrating TRUE)
elseif(NOT EXISTS "${BUDGET_FILE}")
  # A backend nobody has measured on this kind of machine yet (the GL budget
  # needs a real GPU): report the numbers instead of inventing a limit.
  message("FRAME_BUDGET_UNCALIBRATED: no budget file ${BUDGET_FILE}"
    " for renderBackend=${RENDER_BACKEND}; run with -DCALIBRATE=1 to produce one")
  set(_calibrating TRUE)
else()
  set(_calibrating FALSE)
  file(STRINGS "${BUDGET_FILE}" _budget_lines
    REGEX "^[A-Za-z_][A-Za-z0-9_]*=[0-9]+$")
  foreach(_budget_line ${_budget_lines})
    string(REGEX MATCH "^([^=]+)=([0-9]+)$" _budget_match "${_budget_line}")
    set("_budget_${CMAKE_MATCH_1}" "${CMAKE_MATCH_2}")
  endforeach()
  foreach(_required blend_kpx restore_kpx fill_kpx round_kpx grad_kpx
                   total_kpx style_resolve style_miss render_ms worst_ms tree_ms
                   surface_w surface_h first_whole_ms first_tree_ms
                   first_blend_kpx first_style_miss partial_min part_render_ms
                   slow16_max)
    if(NOT DEFINED "_budget_${_required}")
      message(FATAL_ERROR "frame budget ${BUDGET_FILE} is missing ${_required}")
    endif()
  endforeach()
endif()

function(_field LINE KEY DEFAULT OUT)
  string(REGEX MATCH "${KEY}=([0-9]+(\\.[0-9]+)?)" _field_match "${LINE}")
  if(_field_match)
    set(${OUT} "${CMAKE_MATCH_1}" PARENT_SCOPE)
  else()
    set(${OUT} "${DEFAULT}" PARENT_SCOPE)
  endif()
endfunction()

function(_raster_kpx LINE KEY OUT)
  string(REGEX MATCH "${KEY}=[^/ ]*/([0-9]+)kpx" _raster_match "${LINE}")
  if(_raster_match)
    set(${OUT} "${CMAKE_MATCH_1}" PARENT_SCOPE)
  else()
    set(${OUT} "0" PARENT_SCOPE)
  endif()
endfunction()

_field("${_first_line}" "whole_ms" "0" _first_whole)
_field("${_first_line}" "render_ms" "0" _first_render)
_field("${_first_line}" "tree_ms" "0" _first_tree)
_field("${_first_line}" "blend" "0" _first_blend)

string(REGEX MATCH "style=([0-9]+)/([0-9]+)miss" _first_style_match "${_first_line}")
if(_first_style_match)
  set(_first_style_miss "${CMAKE_MATCH_2}")
else()
  set(_first_style_miss "0")
endif()

set(_max_blend 0)
set(_max_restore 0)
set(_max_fill 0)
set(_max_round 0)
set(_max_grad 0)
set(_max_total 0)
set(_max_style 0)
set(_max_miss 0)
set(_max_render 0)
set(_max_worst 0)
set(_max_tree 0)
# 局部帧下限（与其余上限相反）：一批 30 帧里至少这么多帧只重绘损伤区。
# 状态变化改回整窗重绘会让它掉下来，光看 render_ms 均值是看不出的。
set(_min_partial 30)
# 局部帧自己的耗时：稳态下整窗帧归零后 render_ms 与像素计数器都测不
# 到东西（它们只统计整窗帧），局部帧变贵就没人拦了。
set(_max_part_render 0)
# 一批 30 帧里超过 16.7ms 的帧数：worst_ms 只看最慢那一帧，掉 1 帧和掉
# 12 帧给出同一个数字，而手感上的一卡一卡是掉帧密度。
set(_max_slow16 0)
list(LENGTH _frame_indexes _batch_count)
math(EXPR _steady_start "2")
# The LAST batch is not steady state either: the driver script ends with
# freeze + `redraw full` + three pixel dumps, so its tail is whole-window by
# construction (those forced frames are what the A/B/C pixel comparison
# snapshots). Counting them here would pin blend/grad/restore to whatever a
# deliberate full repaint costs and hide every real regression behind it.
math(EXPR _steady_last "${_batch_count} - 2")
if(_batch_count LESS 4)
  message(FATAL_ERROR "no steady-state frame batches remain after dropping two")
endif()
math(EXPR _last_batch "${_batch_count} - 1")
foreach(_batch RANGE ${_steady_start} ${_steady_last})
  list(GET _frame_indexes ${_batch} _frame_index)
  math(EXPR _raster_index "${_frame_index} + 1")
  if(_raster_index GREATER_EQUAL _line_count)
    message(FATAL_ERROR "frames=30 at line ${_frame_index} has no following raster/frame line")
  endif()
  list(GET _lines ${_frame_index} _frame_line)
  list(GET _lines ${_raster_index} _raster_line)
  if(NOT _raster_line MATCHES "raster/frame ")
    message(FATAL_ERROR "frames=30 at line ${_frame_index} is not followed by raster/frame")
  endif()
  string(REGEX MATCH "surface=([0-9]+)x([0-9]+)" _surface_match "${_frame_line}")
  if(NOT _surface_match)
    string(REGEX MATCH "surface=([0-9]+)x([0-9]+)" _surface_match "${_raster_line}")
  endif()
  if(NOT _surface_match)
    message(FATAL_ERROR "steady-state frames=30 at line ${_frame_index} has no surface size")
  endif()
  set(_batch_surface_w "${CMAKE_MATCH_1}")
  set(_batch_surface_h "${CMAKE_MATCH_2}")
  if(NOT _surface_seen)
    set(_surface_width "${_batch_surface_w}")
    set(_surface_height "${_batch_surface_h}")
    set(_surface_seen TRUE)
  elseif(NOT _batch_surface_w EQUAL _surface_width OR
         NOT _batch_surface_h EQUAL _surface_height)
    set(_surface_mismatch TRUE)
  endif()
  _raster_kpx("${_raster_line}" "fill" _fill)
  _raster_kpx("${_raster_line}" "blend" _blend)
  _raster_kpx("${_raster_line}" "round" _round)
  _raster_kpx("${_raster_line}" "grad" _grad)
  _raster_kpx("${_raster_line}" "restore" _restore)
  _raster_kpx("${_raster_line}" "glyph" _glyph)
  _raster_kpx("${_raster_line}" "snap" _snap)
  string(REGEX MATCH "style=([0-9]+)/([0-9]+)miss" _style_match "${_raster_line}")
  if(_style_match)
    set(_style "${CMAKE_MATCH_1}")
    set(_miss "${CMAKE_MATCH_2}")
  else()
    set(_style "0")
    set(_miss "0")
  endif()
  _field("${_frame_line}" "part_render_ms" "0" _part_render)
  if(_part_render GREATER _max_part_render)
    set(_max_part_render "${_part_render}")
  endif()
  _field("${_frame_line}" "slow16" "0" _slow16)
  if(_slow16 GREATER _max_slow16)
    set(_max_slow16 "${_slow16}")
  endif()
  _field("${_frame_line}" "partial" "0" _partial)
  if(_partial LESS _min_partial)
    set(_min_partial "${_partial}")
  endif()
  _field("${_frame_line}" "render_ms" "0" _render)
  _field("${_frame_line}" "worst_ms" "0" _worst)
  _field("${_frame_line}" "tree_ms" "0" _tree)
  math(EXPR _total "${_fill} + ${_blend} + ${_round} + ${_grad} + ${_restore} + ${_glyph} + ${_snap}")
  if(_blend GREATER _max_blend)
    set(_max_blend "${_blend}")
  endif()
  if(_restore GREATER _max_restore)
    set(_max_restore "${_restore}")
  endif()
  if(_fill GREATER _max_fill)
    set(_max_fill "${_fill}")
  endif()
  if(_round GREATER _max_round)
    set(_max_round "${_round}")
  endif()
  if(_grad GREATER _max_grad)
    set(_max_grad "${_grad}")
  endif()
  if(_total GREATER _max_total)
    set(_max_total "${_total}")
  endif()
  if(_style GREATER _max_style)
    set(_max_style "${_style}")
  endif()
  if(_miss GREATER _max_miss)
    set(_max_miss "${_miss}")
  endif()
  if(_render GREATER _max_render)
    set(_max_render "${_render}")
  endif()
  if(_worst GREATER _max_worst)
    set(_max_worst "${_worst}")
  endif()
  if(_tree GREATER _max_tree)
    set(_max_tree "${_tree}")
  endif()
endforeach()

if(_calibrating)
  # Budget-file syntax, so a calibration run's output can be saved as one.
  message("FRAME_BUDGET_MEASURED renderBackend=${RENDER_BACKEND}"
    " partialFrames=${PARTIAL_FRAMES} (paste into a budget file, headroom is"
    " yours to add)")
  foreach(_measured
      "surface_w;${_surface_width}"
      "surface_h;${_surface_height}"
      "blend_kpx;${_max_blend}"
      "restore_kpx;${_max_restore}"
      "fill_kpx;${_max_fill}"
      "round_kpx;${_max_round}"
      "grad_kpx;${_max_grad}"
      "total_kpx;${_max_total}"
      "style_resolve;${_max_style}"
      "style_miss;${_max_miss}"
      "render_ms;${_max_render}"
      "worst_ms;${_max_worst}"
      "tree_ms;${_max_tree}"
      "slow16_max;${_max_slow16}"
      "partial_min;${_min_partial}"
      "part_render_ms;${_max_part_render}"
      "first_whole_ms;${_first_whole}"
      "first_tree_ms;${_first_tree}"
      "first_blend_kpx;${_first_blend}"
      "first_style_miss;${_first_style_miss}")
    list(GET _measured 0 _name)
    list(GET _measured 1 _value)
    message("${_name}=${_value}")
  endforeach()
  if(_surface_mismatch)
    message("FRAME_BUDGET_SKIPPED: surface size changed mid-run, calibrate again")
    return()
  endif()
  # ctest's SKIP_REGULAR_EXPRESSION: an uncalibrated backend is not a failure.
  message("FRAME_BUDGET_SKIPPED: calibration run, nothing asserted")
  return()
endif()

message("FRAME_BUDGET measured vs budget (renderBackend=${RENDER_BACKEND})")
message("  surface      ${_surface_width}x${_surface_height} / ${_budget_surface_w}x${_budget_surface_h}")
if(_surface_mismatch OR
   NOT _surface_width EQUAL _budget_surface_w OR
   NOT _surface_height EQUAL _budget_surface_h)
  message("FRAME_BUDGET_SKIPPED: surface ${_surface_width}x${_surface_height} does not match expected ${_budget_surface_w}x${_budget_surface_h}")
  return()
endif()
message("  blend_kpx    ${_max_blend} / ${_budget_blend_kpx}")
message("  restore_kpx  ${_max_restore} / ${_budget_restore_kpx}")
message("  fill_kpx     ${_max_fill} / ${_budget_fill_kpx}")
message("  round_kpx    ${_max_round} / ${_budget_round_kpx}")
message("  grad_kpx     ${_max_grad} / ${_budget_grad_kpx}")
message("  total_kpx    ${_max_total} / ${_budget_total_kpx}")
message("  style_resolve ${_max_style} / ${_budget_style_resolve}")
message("  style_miss   ${_max_miss} / ${_budget_style_miss}")
message("  render_ms    ${_max_render} / ${_budget_render_ms}")
message("  worst_ms     ${_max_worst} / ${_budget_worst_ms}")
message("  tree_ms      ${_max_tree} / ${_budget_tree_ms}")
message("  slow16       ${_max_slow16} / ${_budget_slow16_max}")
message("  partial_min  ${_min_partial} / ${_budget_partial_min} (floor)")
message("  part_render_ms ${_max_part_render} / ${_budget_part_render_ms}")
message("  first_whole_ms ${_first_whole} / ${_budget_first_whole_ms}")
message("  first_tree_ms  ${_first_tree} / ${_budget_first_tree_ms}")
message("  first_blend_kpx ${_first_blend} / ${_budget_first_blend_kpx}")
message("  first_style_miss ${_first_style_miss} / ${_budget_first_style_miss}")

set(_failed "")
foreach(_check
    "blend_kpx;${_max_blend};${_budget_blend_kpx}"
    "restore_kpx;${_max_restore};${_budget_restore_kpx}"
    "fill_kpx;${_max_fill};${_budget_fill_kpx}"
    "round_kpx;${_max_round};${_budget_round_kpx}"
    "grad_kpx;${_max_grad};${_budget_grad_kpx}"
    "total_kpx;${_max_total};${_budget_total_kpx}"
    "style_resolve;${_max_style};${_budget_style_resolve}"
    "style_miss;${_max_miss};${_budget_style_miss}"
    "render_ms;${_max_render};${_budget_render_ms}"
    "worst_ms;${_max_worst};${_budget_worst_ms}"
    "tree_ms;${_max_tree};${_budget_tree_ms}"
    "first_whole_ms;${_first_whole};${_budget_first_whole_ms}"
    "first_tree_ms;${_first_tree};${_budget_first_tree_ms}"
    "first_blend_kpx;${_first_blend};${_budget_first_blend_kpx}"
    "first_style_miss;${_first_style_miss};${_budget_first_style_miss}"
    "part_render_ms;${_max_part_render};${_budget_part_render_ms}"
    "slow16;${_max_slow16};${_budget_slow16_max}")
  list(GET _check 0 _name)
  list(GET _check 1 _value)
  list(GET _check 2 _limit)
  if(_value GREATER _limit)
    list(APPEND _failed "${_name}=${_value} (budget ${_limit})")
  endif()
endforeach()
if(_min_partial LESS _budget_partial_min)
  list(APPEND _failed "partial_min=${_min_partial} (floor ${_budget_partial_min})")
endif()
if(_failed)
  message(FATAL_ERROR "FRAME_BUDGET_EXCEEDED: ${_failed}")
endif()
message("FRAME_BUDGET_OK")

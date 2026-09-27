# Embedded-resource deflate compression, end to end:
#   1. bake a 1200-byte text asset (--embed) that MUST compress (>= 512
#      threshold, highly repetitive) and a 96-byte one that stays raw,
#   2. publish the program,
#   3. assert the exe carries NO plaintext of the asset (the whole point of
#      the feature -- if compression regressed to a raw bake this fails),
#   4. run it from a directory that does NOT contain the assets, so every
#      read goes through the embedded copy (disk-first would hide the whole
#      pipeline), and require the exact golden output.
#
# Invoked as:
#   cmake -DZANC=<zanc> -DSRC=<file.zan> -DEXPECTED=<golden> -DOUT_EXE=<path>
#         -DWORKDIR=<dir> -P run_embed_compress.cmake

cmake_policy(SET CMP0012 NEW)
if(NOT ZANC OR NOT SRC OR NOT EXPECTED OR NOT OUT_EXE OR NOT WORKDIR)
  message(FATAL_ERROR "run_embed_compress.cmake: ZANC, SRC, EXPECTED, OUT_EXE and WORKDIR are required")
endif()

set(asset_dir ${WORKDIR}/embed_compress_assets)
set(run_dir ${WORKDIR}/embed_compress_run)
file(REMOVE_RECURSE ${asset_dir} ${run_dir})
file(MAKE_DIRECTORY ${asset_dir}/assets ${run_dir})

string(REPEAT "THE-QUICK-BROWN-FOX-" 60 big_content)
string(REPEAT "SMALL-RAW-ASSET-" 6 small_content)
file(WRITE ${asset_dir}/assets/big.txt ${big_content})
file(WRITE ${asset_dir}/assets/small.txt ${small_content})

execute_process(
  COMMAND ${ZANC} ${SRC} --publish --embed ${asset_dir}/assets -o ${OUT_EXE}
  RESULT_VARIABLE compile_rc
  OUTPUT_VARIABLE compile_out
  ERROR_VARIABLE  compile_err)
if(NOT compile_rc EQUAL 0)
  message(FATAL_ERROR "compile failed (rc=${compile_rc})\n${compile_out}${compile_err}")
endif()

# No plaintext of the asset content may survive in the image.
file(READ ${OUT_EXE} image)
string(FIND ${image} "THE-QUICK-BROWN-FOX" hit)
if(NOT hit EQUAL -1)
  message(FATAL_ERROR "the 1200-byte asset is baked uncompressed; deflate compression did not engage")
endif()

execute_process(
  COMMAND ${OUT_EXE}
  WORKING_DIRECTORY ${run_dir}
  RESULT_VARIABLE run_rc
  OUTPUT_VARIABLE run_out
  ERROR_VARIABLE  run_err)
if(NOT run_rc EQUAL 0)
  message(FATAL_ERROR "run failed (rc=${run_rc})\nstdout:\n${run_out}\nstderr:\n${run_err}")
endif()

file(READ ${EXPECTED} expected)
string(REPLACE "\r\n" "\n" expected "${expected}")
string(REPLACE "\r\n" "\n" actual   "${run_out}")
string(REGEX REPLACE "[ \t\r\n]+$" "" expected "${expected}")
string(REGEX REPLACE "[ \t\r\n]+$" "" actual   "${actual}")
if(NOT actual STREQUAL expected)
  message(FATAL_ERROR "output mismatch for ${SRC}\n--- expected ---\n${expected}\n--- actual ---\n${actual}")
endif()

file(REMOVE_RECURSE ${asset_dir} ${run_dir})
message(STATUS "embed_compress: bake is compressed and decodes byte-exact")

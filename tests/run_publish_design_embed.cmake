# Check explicit HTML resources in a published executable.
# Stage the page and an undeclared src/docs/hidden.html in a mini project.
# Publish the page under two aliases that share a payload, then check the
# executable for plaintext and compare the VFS reads against the golden output
# from a working directory without loose HTML files.
#
# Invoked as:
#   cmake -DZANC=<zanc> -DSRC=<file.zan> -DEXPECTED=<golden> -DOUT_EXE=<path>
#         -DWORKDIR=<dir> -P run_publish_design_embed.cmake

cmake_policy(SET CMP0012 NEW)
if(NOT ZANC OR NOT SRC OR NOT EXPECTED OR NOT OUT_EXE OR NOT WORKDIR)
  message(FATAL_ERROR "run_publish_design_embed.cmake: ZANC, SRC, EXPECTED, OUT_EXE and WORKDIR are required")
endif()

set(proj_dir ${WORKDIR}/pub_design/proj)
set(run_dir ${WORKDIR}/pub_design/run)
file(REMOVE_RECURSE ${WORKDIR}/pub_design)
file(MAKE_DIRECTORY ${proj_dir}/src/docs ${run_dir})

file(WRITE ${proj_dir}/zan.proj "name = pub_design_probe\n")
file(WRITE ${proj_dir}/src/page.html "<html><body>secret-page-9137</body></html>")
file(WRITE ${proj_dir}/src/docs/hidden.html "<html>must-not-embed</html>")
configure_file(${SRC} ${proj_dir}/app.zan COPYONLY)

execute_process(
  COMMAND ${ZANC} ${proj_dir}/app.zan --publish
    --embed "${proj_dir}/src/page.html=src/page.html"
    --embed "${proj_dir}/src/page.html=views/start.html" -o ${OUT_EXE}
  RESULT_VARIABLE compile_rc
  OUTPUT_VARIABLE compile_out
  ERROR_VARIABLE  compile_err)
if(NOT compile_rc EQUAL 0)
  message(FATAL_ERROR "compile failed (rc=${compile_rc})\n${compile_out}${compile_err}")
endif()

# The design's plaintext may not survive in the image.
# HEX read: text-mode file(READ) stops at the first NUL byte and any 0x3B
# in the image list-splits the variable into extra FIND parameters.
file(READ ${OUT_EXE} image HEX)
string(FIND "${image}" "7365637265742d706167652d39313337" hit)
if(NOT hit EQUAL -1)
  message(FATAL_ERROR "the design html is baked as plaintext; publish encryption did not engage")
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

file(REMOVE_RECURSE ${WORKDIR}/pub_design)
message(STATUS "publish_design_embed: explicit aliases, encryption and embedded-VFS reads verified")

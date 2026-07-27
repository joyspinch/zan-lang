# Drives zanpkg through a whole project lifecycle in a scratch directory:
# init, add/remove a dependency, info, and a real build whose binary must run.
#
#   cmake -DZANPKG=<exe> -DZANC_DIR=<dir holding zanc> -DWORKDIR=<scratch> \
#         -P run_zanpkg.cmake

if(NOT ZANPKG OR NOT ZANC_DIR OR NOT WORKDIR)
  message(FATAL_ERROR "run_zanpkg.cmake: ZANPKG, ZANC_DIR and WORKDIR are required")
endif()

file(REMOVE_RECURSE ${WORKDIR})
file(MAKE_DIRECTORY ${WORKDIR})

function(zanpkg_run out_var)
  execute_process(COMMAND ${ZANPKG} ${ARGN}
    WORKING_DIRECTORY ${WORKDIR}
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err ENCODING UTF-8)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "zanpkg ${ARGN} failed (rc=${rc})\n${out}${err}")
  endif()
  set(${out_var} "${out}" PARENT_SCOPE)
endfunction()

zanpkg_run(out init demo)
if(NOT EXISTS ${WORKDIR}/zan.pkg OR NOT EXISTS ${WORKDIR}/src/Program.zan)
  message(FATAL_ERROR "init did not create the manifest and entry point")
endif()

# init refuses to clobber an existing manifest
zanpkg_run(out init demo)
if(NOT out MATCHES "already exists")
  message(FATAL_ERROR "a second init did not report the existing manifest:\n${out}")
endif()

zanpkg_run(out add fancy "github/fancy@1.0")
file(READ ${WORKDIR}/zan.pkg manifest)
if(NOT manifest MATCHES "fancy = \"github/fancy@1.0\"")
  message(FATAL_ERROR "add did not record the dependency:\n${manifest}")
endif()

zanpkg_run(out info)
if(NOT out MATCHES "name = \"demo\"" OR NOT out MATCHES "fancy")
  message(FATAL_ERROR "info did not list the manifest:\n${out}")
endif()

zanpkg_run(out remove fancy)
file(READ ${WORKDIR}/zan.pkg manifest)
if(manifest MATCHES "fancy")
  message(FATAL_ERROR "remove left the dependency behind:\n${manifest}")
endif()
if(NOT manifest MATCHES "name = \"demo\"")
  message(FATAL_ERROR "remove dropped unrelated manifest lines:\n${manifest}")
endif()

zanpkg_run(out remove nothere)
if(NOT out MATCHES "not found")
  message(FATAL_ERROR "removing an absent dependency was reported as a removal:\n${out}")
endif()

# build shells out to zanc, so it has to be on PATH
if(WIN32)
  set(ENV{PATH} "${ZANC_DIR};$ENV{PATH}")
else()
  set(ENV{PATH} "${ZANC_DIR}:$ENV{PATH}")
endif()

zanpkg_run(out build)
if(NOT out MATCHES "Build successful")
  message(FATAL_ERROR "build did not succeed:\n${out}")
endif()

if(WIN32)
  set(built ${WORKDIR}/build/demo.exe)
else()
  set(built ${WORKDIR}/build/demo)
endif()
if(NOT EXISTS ${built})
  message(FATAL_ERROR "build did not produce ${built}")
endif()

execute_process(COMMAND ${built} RESULT_VARIABLE rc OUTPUT_VARIABLE ran ENCODING UTF-8)
if(NOT rc EQUAL 0 OR NOT ran MATCHES "Hello from demo!")
  message(FATAL_ERROR "the built program did not run (rc=${rc}):\n${ran}")
endif()

message(STATUS "OK: zanpkg")

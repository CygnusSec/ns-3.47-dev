# Keep CATRA experiments in one source namespace while producing independent
# executables.  The parent scratch auto-discovery cannot infer two main()
# functions from one directory, so each program is declared explicitly here.

build_exec(
  EXECNAME catra-phy-range-probe
  SOURCE_FILES catra-phy-range-probe.cc
  LIBRARIES_TO_LINK "${ns3-libs}"
                    "${ns3-contrib-libs}"
  EXECUTABLE_DIRECTORY_PATH ${CMAKE_OUTPUT_DIRECTORY}/scratch/catra
)

if(TARGET active-time)
  build_exec(
    EXECNAME catra-active-time-probe
    # Match the target name inferred by ./ns3 for a main source stored in the
    # nested active-time-estimation directory.
    EXECNAME_PREFIX scratch_catra_active-time-estimation_
    SOURCE_FILES active-time-estimation/catra-active-time-probe.cc
    LIBRARIES_TO_LINK "${ns3-libs}"
                      "${ns3-contrib-libs}"
                      "${libactive-time}"
    EXECUTABLE_DIRECTORY_PATH ${CMAKE_OUTPUT_DIRECTORY}/scratch/catra/active-time-estimation
  )
endif()

if(TARGET catra)
  build_exec(
    EXECNAME catra-tcp-controller-probe
    EXECNAME_PREFIX scratch_catra_tcp_rate_adaptation_
    SOURCE_FILES tcp_rate_adaptation/catra-tcp-controller-probe.cc
    LIBRARIES_TO_LINK "${ns3-libs}"
                      "${ns3-contrib-libs}"
                      "${libcatra}"
    EXECUTABLE_DIRECTORY_PATH ${CMAKE_OUTPUT_DIRECTORY}/scratch/catra/tcp_rate_adaptation
  )
else()
  message(STATUS "CATRA module disabled: controller probe skipped; common active-time measurement and Scenario 1 remain available")
endif()

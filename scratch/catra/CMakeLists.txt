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

# Scenario 1 is also the non-CATRA TCP Tahoe baseline. Keep this target
# available in both module configurations and add the shared CATRA dependency
# only when the contributed module was enabled by ./ns3 configure.
# Do not hide Scenario 1's dependencies behind the aggregate ns3-libs list.
# This executable uses application helpers, FlowMonitor and debug-tools in
# addition to CATRA's own core/network/internet/wifi dependencies.
set(scenario1_libraries
    ${libapplications}
    ${libcore}
    ${libdebug-tools}
    ${libflow-monitor}
    ${libinternet}
    ${libmobility}
    ${libnetwork}
    ${libpropagation}
    ${libwifi}
    ${libdebug-tools}
)
set(scenario1_definitions)
if(TARGET catra)
  list(APPEND scenario1_libraries "${libcatra}")
  list(APPEND scenario1_definitions NS3_CATRA_MODULE_ENABLED=1)
endif()

build_exec(
  EXECNAME catra-scenario1
  EXECNAME_PREFIX scratch_catra_scenario1_
  SOURCE_FILES scenario1/scenario1-main.cc
               scenario1/baseline/scenario1-baseline.cc
               scenario1/baseline/scenario1-mac-hop-measurement.cc
               scenario1/baseline/tcp-tahoe.cc
               scenario1/catra/scenario1-catra.cc
               scenario1/catra/scenario1-contention.cc
  LIBRARIES_TO_LINK ${scenario1_libraries}
  DEFINITIONS ${scenario1_definitions}
  EXECUTABLE_DIRECTORY_PATH ${CMAKE_OUTPUT_DIRECTORY}/scratch/catra/scenario1
)

if(TARGET catra)
  build_exec(
    EXECNAME catra-active-time-probe
    # Match the target name inferred by ./ns3 for a main source stored in the
    # nested active-time-estimation directory.
    EXECNAME_PREFIX scratch_catra_active-time-estimation_
    SOURCE_FILES active-time-estimation/catra-active-time-probe.cc
    LIBRARIES_TO_LINK "${ns3-libs}"
                      "${ns3-contrib-libs}"
                      "${libcatra}"
    EXECUTABLE_DIRECTORY_PATH ${CMAKE_OUTPUT_DIRECTORY}/scratch/catra/active-time-estimation
  )

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
  message(STATUS "CATRA module disabled: Scenario 1 baseline remains available; CATRA probes skipped")
endif()

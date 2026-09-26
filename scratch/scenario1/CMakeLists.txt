# Scenario 1 is one common TCP Tahoe simulation. The CATRA module is an
# optional configure-time enhancement, not a separate executable or source
# tree.
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
)
set(scenario1_definitions)
if(TARGET catra)
  list(APPEND scenario1_libraries "${libcatra}")
  list(APPEND scenario1_definitions NS3_CATRA_MODULE_ENABLED=1)
endif()

build_exec(
  EXECNAME scenario1
  EXECNAME_PREFIX scratch_scenario1_
  SOURCE_FILES scenario1-main.cc
               scenario1-traffic.cc
               scenario1-mac-hop-measurement.cc
               scenario1-radio.cc
               scenario1-cw-trace.cc
               tcp-tahoe.cc
  LIBRARIES_TO_LINK ${scenario1_libraries}
  DEFINITIONS ${scenario1_definitions}
  EXECUTABLE_DIRECTORY_PATH ${CMAKE_OUTPUT_DIRECTORY}/scratch/scenario1
)

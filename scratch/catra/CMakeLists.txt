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

build_exec(
  EXECNAME catra-scenario1
  SOURCE_FILES catra-scenario1.cc
  LIBRARIES_TO_LINK "${ns3-libs}"
                    "${ns3-contrib-libs}"
  EXECUTABLE_DIRECTORY_PATH ${CMAKE_OUTPUT_DIRECTORY}/scratch/catra
)

build_exec(
  EXECNAME catra-active-time-probe
  SOURCE_FILES catra-active-time-probe.cc
               catra-active-time-estimator.cc
  LIBRARIES_TO_LINK "${ns3-libs}"
                    "${ns3-contrib-libs}"
  EXECUTABLE_DIRECTORY_PATH ${CMAKE_OUTPUT_DIRECTORY}/scratch/catra
)

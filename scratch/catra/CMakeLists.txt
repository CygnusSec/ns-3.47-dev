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
  # Match the target name inferred by ./ns3 for a main source stored in the
  # nested active-time-estimation directory.
  EXECNAME_PREFIX scratch_catra_active-time-estimation_
  SOURCE_FILES active-time-estimation/catra-active-time-probe.cc
               active-time-estimation/catra-active-time-estimator.cc
               active-time-estimation/catra-algorithm-packet.cc
               active-time-estimation/catra-mac-transaction-tracker.cc
               active-time-estimation/observe-mac-frame.cc
  LIBRARIES_TO_LINK "${ns3-libs}"
                    "${ns3-contrib-libs}"
  EXECUTABLE_DIRECTORY_PATH ${CMAKE_OUTPUT_DIRECTORY}/scratch/catra/active-time-estimation
)

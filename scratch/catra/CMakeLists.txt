# Keep CATRA experiments in one source namespace while producing independent
# executables.  The parent scratch auto-discovery cannot infer two main()
# functions from one directory, so each program is declared explicitly here.

# CATRA scratch programs are part of the contributed CATRA module contract.
# When the module is disabled at configure time, do not register these targets.
# This also keeps stale executables out of ns3_runnable_programs, so `./ns3 run`
# cannot launch a CATRA scenario from an earlier build configuration.
if(NOT TARGET catra)
  message(STATUS "Skipping scratch/catra: ns-3 module 'catra' is disabled")
  return()
endif()

build_exec(
  EXECNAME catra-phy-range-probe
  SOURCE_FILES catra-phy-range-probe.cc
  LIBRARIES_TO_LINK "${ns3-libs}"
                    "${ns3-contrib-libs}"
  EXECUTABLE_DIRECTORY_PATH ${CMAKE_OUTPUT_DIRECTORY}/scratch/catra
)

build_exec(
  EXECNAME catra-scenario1
  EXECNAME_PREFIX scratch_catra_scenario1_
  SOURCE_FILES scenario1/scenario1-main.cc
               scenario1/baseline/scenario1-baseline.cc
               scenario1/baseline/scenario1-mac-hop-measurement.cc
               scenario1/baseline/tcp-tahoe.cc
               scenario1/catra/scenario1-catra.cc
               scenario1/catra/scenario1-contention.cc
  LIBRARIES_TO_LINK "${ns3-libs}"
                    "${ns3-contrib-libs}"
                    "${libcatra}"
  EXECUTABLE_DIRECTORY_PATH ${CMAKE_OUTPUT_DIRECTORY}/scratch/catra/scenario1
)

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

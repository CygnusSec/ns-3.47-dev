# Build the minimal logging-only tutorial and link only the core module.
build_example(
  NAME hello-simulator
  SOURCE_FILES hello-simulator.cc
  LIBRARIES_TO_LINK ${libcore}
)

# Build the two-node UDP Echo example and link every module it uses.
build_example(
  NAME first
  SOURCE_FILES first.cc
  LIBRARIES_TO_LINK
    ${libcore}
    ${libpoint-to-point}
    ${libinternet}
    ${libapplications}
)

# Build the routed point-to-point plus CSMA example.
build_example(
  NAME second
  SOURCE_FILES second.cc
  LIBRARIES_TO_LINK
    ${libcore}
    ${libpoint-to-point}
    ${libcsma}
    ${libinternet}
    ${libapplications}
    ${libdebug-tools}
)

# Build the combined Wi-Fi, point-to-point, and CSMA topology.
build_example(
  NAME third
  SOURCE_FILES third.cc
  LIBRARIES_TO_LINK
    ${libcore}
    ${libpoint-to-point}
    ${libcsma}
    ${libwifi}
    ${libinternet}
    ${libapplications}
)

# Build the trace-source example; its required APIs are all in core.
build_example(
  NAME fourth
  SOURCE_FILES fourth.cc
  LIBRARIES_TO_LINK ${libcore}
)

# Build the first TCP tracing example together with its custom TutorialApp.
build_example(
  NAME fifth
  SOURCE_FILES fifth.cc
               tutorial-app.cc
  LIBRARIES_TO_LINK
    ${libcore}
    ${libpoint-to-point}
    ${libinternet}
    ${libapplications}
)

# Build the TCP file-output example and reuse TutorialApp.
build_example(
  NAME sixth
  SOURCE_FILES sixth.cc
               tutorial-app.cc
  LIBRARIES_TO_LINK
    ${libcore}
    ${libpoint-to-point}
    ${libinternet}
    ${libapplications}
)

# Build the statistics-helper example and add the stats module dependency.
build_example(
  NAME seventh
  SOURCE_FILES seventh.cc
               tutorial-app.cc
  LIBRARIES_TO_LINK
    ${libcore}
    ${libstats}
    ${libpoint-to-point}
    ${libinternet}
    ${libapplications}
)

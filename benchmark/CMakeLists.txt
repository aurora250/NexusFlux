# NexusFlux usage-side benchmarks.
#
# These use the same headless render path as the SVG/plaintext exports, so the
# numbers cover content building, layout, DOM rendering, and text extraction.

add_executable(nexus-flux-benchmark content_benchmark.cpp)
target_link_libraries(nexus-flux-benchmark PRIVATE NeFlux::NeFlux)
neflux_compiler_options(nexus-flux-benchmark)
set_target_properties(nexus-flux-benchmark PROPERTIES FOLDER "benchmark")
nexusflux_deploy_nexusforce_runtime(nexus-flux-benchmark)

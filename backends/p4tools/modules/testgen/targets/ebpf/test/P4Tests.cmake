# General test utilities.
include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/TestUtils.cmake)
# This file defines how we write the tests we generate.
include(${CMAKE_CURRENT_LIST_DIR}/TestTemplate.cmake)

#############################################################################
# TEST PROGRAMS
#############################################################################
set(
  P4TESTGEN_EBPF_EXCLUDES
  "ebpf_checksum_extern\\.p4"
  "ebpf_conntrack_extern\\.p4"
)

set(EBPF_SEARCH_PATTERNS "include.*ebpf_model.p4")
set(P4TESTS_FOR_EBPF "${P4C_SOURCE_DIR}/testdata/p4_16_samples/*.p4")
p4c_find_tests("${P4TESTS_FOR_EBPF}" EBPF_TESTS INCLUDE "${EBPF_SEARCH_PATTERNS}" EXCLUDE "")
p4tools_find_tests("${EBPF_TESTS}" ebpftests EXCLUDE "${P4TESTGEN_EBPF_EXCLUDES}")

# Add ebpf tests from p4c
set(P4C_EBPF_TEST_SUITES_P416 ${ebpftests})
#############################################################################
# TEST SUITES
#############################################################################
option(P4TOOLS_TESTGEN_EBPF_TEST_STF "Run tests on the STF test back end" ON)
# Test settings.
set(EXTRA_OPTS "--strict --print-traces --seed 1000 --max-tests 10")
set(P4TESTGEN_DRIVER "${P4TOOLS_BINARY_DIR}/p4testgen")

if(P4TOOLS_TESTGEN_EBPF_TEST_STF)
  p4tools_add_tests(
    TESTSUITES "${P4C_EBPF_TEST_SUITES_P416}"
    TAG "testgen-p4c-ebpf" DRIVER ${P4TESTGEN_DRIVER}
    TARGET "ebpf" ARCH "ebpf" ENABLE_RUNNER RUNNER_ARGS "" TEST_ARGS "-I${P4C_BINARY_DIR}/p4include --test-backend STF ${EXTRA_OPTS} "
  )

  # These tests need special arguments.
  # TODO: This test is disabled because the z3 solver times out on it. Figure out why.
  # We can not use the solver timeout because it crashes, the solver does not interact well with GC.
  # p4tools_add_tests(
  #   TESTSUITES "${P4TESTDATA}/p4_16_samples/ebpf_checksum_extern.p4;"
  #   TAG "testgen-p4c-ebpf" DRIVER ${P4TESTGEN_DRIVER}
  #   TARGET "ebpf" ARCH "ebpf" ENABLE_RUNNER RUNNER_ARGS "--extern-file ${P4C_SOURCE_DIR}/testdata/extern_modules/extern-checksum-ebpf.c"
  #   TEST_ARGS "-I${P4C_BINARY_DIR}/p4include --test-backend STF ${EXTRA_OPTS} "
  # )

  p4tools_add_tests(
    TESTSUITES "${P4TESTDATA}/p4_16_samples/ebpf_conntrack_extern.p4;"
    TAG "testgen-p4c-ebpf" DRIVER ${P4TESTGEN_DRIVER}
    TARGET "ebpf" ARCH "ebpf" ENABLE_RUNNER RUNNER_ARGS "--extern-file ${P4C_SOURCE_DIR}/testdata/extern_modules/extern-conntrack-ebpf.c"
    TEST_ARGS "-I${P4C_BINARY_DIR}/p4include --test-backend STF ${EXTRA_OPTS} "
  )
  include(${CMAKE_CURRENT_LIST_DIR}/EBPFXfail.cmake)
endif()

if(EXISTS "/home/runner/work/IRCClient/IRCClient/build-sanitizer/tests/ircclient_tests")
  if(NOT EXISTS "/home/runner/work/IRCClient/IRCClient/build-sanitizer/tests/ircclient_tests-b12d07c_tests.cmake" OR
     NOT "/home/runner/work/IRCClient/IRCClient/build-sanitizer/tests/ircclient_tests-b12d07c_tests.cmake" IS_NEWER_THAN "/home/runner/work/IRCClient/IRCClient/build-sanitizer/tests/ircclient_tests" OR
     NOT "/home/runner/work/IRCClient/IRCClient/build-sanitizer/tests/ircclient_tests-b12d07c_tests.cmake" IS_NEWER_THAN "${CMAKE_CURRENT_LIST_FILE}")
    include("/home/runner/work/IRCClient/IRCClient/build-sanitizer/_deps/catch2-src/extras/CatchAddTests.cmake")
    catch_discover_tests_impl(
      TEST_EXECUTABLE [==[/home/runner/work/IRCClient/IRCClient/build-sanitizer/tests/ircclient_tests]==]
      TEST_EXECUTOR [==[]==]
      TEST_WORKING_DIR [==[/home/runner/work/IRCClient/IRCClient/build-sanitizer/tests]==]
      TEST_SPEC [==[]==]
      TEST_EXTRA_ARGS [==[]==]
      TEST_PROPERTIES [==[ENVIRONMENT;IRCCLIENT_BIN=/home/runner/work/IRCClient/IRCClient/build-sanitizer/ircclient;SKIP_RETURN_CODE;4]==]
      TEST_PREFIX [==['']==]
      TEST_SUFFIX [==['']==]
      TEST_LIST [==[ircclient_tests_TESTS]==]
      TEST_REPORTER [==[]==]
      TEST_OUTPUT_DIR [==[]==]
      TEST_OUTPUT_PREFIX [==[]==]
      TEST_OUTPUT_SUFFIX [==[]==]
      CTEST_FILE [==[/home/runner/work/IRCClient/IRCClient/build-sanitizer/tests/ircclient_tests-b12d07c_tests.cmake]==]
      TEST_DL_PATHS [==[]==]
      TEST_DL_FRAMEWORK_PATHS [==[]==]
      ADD_TAGS_AS_LABELS [==[FALSE]==]
    )
  endif()
  include("/home/runner/work/IRCClient/IRCClient/build-sanitizer/tests/ircclient_tests-b12d07c_tests.cmake")
else()
  add_test(ircclient_tests_NOT_BUILT ircclient_tests_NOT_BUILT)
endif()

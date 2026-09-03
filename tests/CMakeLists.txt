file(GLOB ircclient_unit_sources CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp"
)
file(GLOB ircclient_e2e_sources CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/e2e/*.cpp"
)
file(GLOB ircclient_support_sources CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/support/*.cpp"
)

add_executable(ircclient_tests
  ${ircclient_unit_sources}
  ${ircclient_e2e_sources}
  ${ircclient_support_sources}
)
target_link_libraries(ircclient_tests
  PRIVATE
    ircclient_core
    Catch2::Catch2WithMain
)
add_dependencies(ircclient_tests ircclient)
ircclient_set_warnings(ircclient_tests)
ircclient_apply_instrumentation(ircclient_tests)

include(Catch)
catch_discover_tests(ircclient_tests
  DISCOVERY_MODE PRE_TEST
  PROPERTIES ENVIRONMENT "IRCCLIENT_BIN=$<TARGET_FILE:ircclient>"
)

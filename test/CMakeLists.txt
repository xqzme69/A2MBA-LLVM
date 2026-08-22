set(A2MBA_SELFTEST_SOURCES
  tools/a2mba-selftest.cpp
  "${PROJECT_SOURCE_DIR}/lib/Modular.cpp"
  "${PROJECT_SOURCE_DIR}/lib/Random.cpp"
)

add_executable(a2mba-selftest ${A2MBA_SELFTEST_SOURCES})
target_compile_features(a2mba-selftest PRIVATE cxx_std_20)
target_compile_options(a2mba-selftest PRIVATE ${LLVM_DEFINITIONS_LIST})
target_include_directories(a2mba-selftest
  PRIVATE "${PROJECT_SOURCE_DIR}/include"
)
target_include_directories(a2mba-selftest SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})

llvm_map_components_to_libnames(A2MBA_SELFTEST_LLVM_LIBS
  Core
  Support
)
target_link_libraries(a2mba-selftest PRIVATE ${A2MBA_SELFTEST_LLVM_LIBS})

if(WIN32)
  target_link_libraries(a2mba-selftest PRIVATE bcrypt)
endif()

a2mba_enable_sanitizers(a2mba-selftest)

set(A2MBA_TEST_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(A2MBA_TEST_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}")
set(A2MBA_WRAPPER_PATH "${PROJECT_SOURCE_DIR}/tools/a2mba-clang.py")
if(LLVM_DEFAULT_TARGET_TRIPLE)
  set(A2MBA_HOST_TRIPLE "${LLVM_DEFAULT_TARGET_TRIPLE}")
elseif(LLVM_HOST_TRIPLE)
  set(A2MBA_HOST_TRIPLE "${LLVM_HOST_TRIPLE}")
else()
  set(A2MBA_HOST_TRIPLE "${LLVM_TARGET_TRIPLE}")
endif()

configure_lit_site_cfg(
  "${PROJECT_SOURCE_DIR}/cmake/lit.site.cfg.py.in"
  "${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg.py"
  MAIN_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/lit.cfg.py"
)

if(LLVM_EXTERNAL_LIT AND EXISTS "${LLVM_EXTERNAL_LIT}")
  set(A2MBA_LIT_PROGRAM "${LLVM_EXTERNAL_LIT}")
elseif(LLVM_EXTERNAL_LIT)
  message(FATAL_ERROR "LLVM_EXTERNAL_LIT does not exist: ${LLVM_EXTERNAL_LIT}")
elseif(EXISTS "${LLVM_TOOLS_BINARY_DIR}/llvm-lit.py")
  set(A2MBA_LIT_PROGRAM "${LLVM_TOOLS_BINARY_DIR}/llvm-lit.py")
elseif(EXISTS "${LLVM_TOOLS_BINARY_DIR}/llvm-lit")
  set(A2MBA_LIT_PROGRAM "${LLVM_TOOLS_BINARY_DIR}/llvm-lit")
endif()

if(A2MBA_LIT_PROGRAM)
  if(A2MBA_LIT_PROGRAM MATCHES "\\.py$")
    set(A2MBA_LIT_COMMAND "${Python3_EXECUTABLE}" "${A2MBA_LIT_PROGRAM}")
  else()
    set(A2MBA_LIT_COMMAND "${A2MBA_LIT_PROGRAM}")
  endif()
else()
  get_filename_component(A2MBA_PYTHON_BIN_DIR
    "${Python3_EXECUTABLE}" DIRECTORY
  )
  find_program(A2MBA_PYTHON_LIT_PROGRAM
    NAMES lit
    HINTS
      "${A2MBA_PYTHON_BIN_DIR}"
      "${A2MBA_PYTHON_BIN_DIR}/Scripts"
    NO_DEFAULT_PATH
  )
  if(NOT A2MBA_PYTHON_LIT_PROGRAM)
    message(FATAL_ERROR
      "llvm-lit was not found. Set LLVM_EXTERNAL_LIT or install the "
      "Python 'lit' package."
    )
  endif()
  set(A2MBA_LIT_COMMAND "${A2MBA_PYTHON_LIT_PROGRAM}")
endif()

set(A2MBA_LIT_ARGS
  -sv
  --param "a2mba_plugin=$<TARGET_FILE:A2MBA>"
  --param "a2mba_selftest=$<TARGET_FILE:a2mba-selftest>"
  "${CMAKE_CURRENT_BINARY_DIR}"
)

add_custom_target(check-a2mba
  COMMAND ${A2MBA_LIT_COMMAND} ${A2MBA_LIT_ARGS}
  DEPENDS A2MBA a2mba-selftest
  COMMENT "Running A2MBA lit tests"
  USES_TERMINAL
  VERBATIM
)

add_test(
  NAME a2mba-lit
  COMMAND ${A2MBA_LIT_COMMAND} ${A2MBA_LIT_ARGS}
  COMMAND_EXPAND_LISTS
)

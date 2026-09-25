cmake_minimum_required(VERSION 3.24)

project(
  A2MBA
  VERSION 0.1.0
  DESCRIPTION "LLVM 21 pass plugin for A2MBA transformations"
  LANGUAGES CXX
)

option(A2MBA_BUILD_TESTS "Build and register the A2MBA test suite" ON)
option(
  A2MBA_ENABLE_SANITIZERS
  "Enable AddressSanitizer and UndefinedBehaviorSanitizer"
  OFF
)
set(A2MBA_FILECHECK_EXECUTABLE "" CACHE FILEPATH
  "Optional path to a FileCheck executable for the lit test suite"
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(LLVM REQUIRED CONFIG)
if(NOT LLVM_VERSION_MAJOR EQUAL 21)
  message(FATAL_ERROR
    "A2MBA requires LLVM major version 21; found ${LLVM_PACKAGE_VERSION}"
  )
endif()

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION} in ${LLVM_DIR}")

# LLVM 21.1.8's portable Windows exports can retain the package builder's
# absolute DIA SDK path. Replace only a missing diaguids.lib entry.
if(WIN32 AND TARGET LLVMDebugInfoPDB)
  get_target_property(A2MBA_PDB_LINK_LIBS
    LLVMDebugInfoPDB INTERFACE_LINK_LIBRARIES
  )
  set(A2MBA_PDB_LINK_LIBS_FIXED)
  foreach(a2mba_pdb_link_lib IN LISTS A2MBA_PDB_LINK_LIBS)
    if(a2mba_pdb_link_lib MATCHES "diaguids\\.lib$" AND
       NOT EXISTS "${a2mba_pdb_link_lib}")
      if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "A2MBA v0.1 supports only Windows x86-64")
      endif()

      find_library(A2MBA_DIAGUIDS_LIBRARY
        NAMES diaguids
        HINTS
          "${CMAKE_GENERATOR_INSTANCE}/DIA SDK/lib/amd64"
          "$ENV{VSINSTALLDIR}/DIA SDK/lib/amd64"
      )
      if(NOT A2MBA_DIAGUIDS_LIBRARY)
        message(FATAL_ERROR
          "LLVM references a missing DIA SDK library. Set "
          "A2MBA_DIAGUIDS_LIBRARY to the x64 diaguids.lib path."
        )
      endif()
      list(APPEND A2MBA_PDB_LINK_LIBS_FIXED "${A2MBA_DIAGUIDS_LIBRARY}")
    else()
      list(APPEND A2MBA_PDB_LINK_LIBS_FIXED "${a2mba_pdb_link_lib}")
    endif()
  endforeach()
  set_property(TARGET LLVMDebugInfoPDB PROPERTY
    INTERFACE_LINK_LIBRARIES "${A2MBA_PDB_LINK_LIBS_FIXED}"
  )
endif()

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")

# AddLLVM otherwise inherits the output directories from the LLVM installation.
set(LLVM_RUNTIME_OUTPUT_INTDIR "${CMAKE_BINARY_DIR}/bin")
set(LLVM_LIBRARY_OUTPUT_INTDIR "${CMAKE_BINARY_DIR}/lib")

include(AddLLVM)
include(GNUInstallDirs)

find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)

set(A2MBA_HYBRID_SOURCE_DIR "${PROJECT_SOURCE_DIR}/lib/HybridCore")
set(A2MBA_HYBRID_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/a2mba-hybrid-generated")
set(A2MBA_HYBRID_VERIFIED_RULES "${A2MBA_HYBRID_GENERATED_DIR}/VerifiedRules.inc")
set(A2MBA_HYBRID_RULE_REPORT "${A2MBA_HYBRID_GENERATED_DIR}/rules-verification.json")

add_custom_command(
  OUTPUT
    "${A2MBA_HYBRID_VERIFIED_RULES}"
    "${A2MBA_HYBRID_RULE_REPORT}"
  COMMAND
    ${Python3_EXECUTABLE}
    "${PROJECT_SOURCE_DIR}/scripts/verify_hybrid_rules.py"
    --spec "${A2MBA_HYBRID_SOURCE_DIR}/mba.json"
    --header "${A2MBA_HYBRID_VERIFIED_RULES}"
    --report "${A2MBA_HYBRID_RULE_REPORT}"
  DEPENDS
    "${A2MBA_HYBRID_SOURCE_DIR}/mba.json"
    "${PROJECT_SOURCE_DIR}/scripts/verify_hybrid_rules.py"
    "${PROJECT_SOURCE_DIR}/scripts/hybrid_smt.py"
  COMMENT "Proving the hybrid MBA rule table at 8/16/32/64 bits"
  VERBATIM
)

add_library(A2MBAHybridCore STATIC
  lib/HybridCore/Core.cpp
  lib/HybridCore/EGraph.cpp
  lib/HybridCore/Extractor.cpp
  lib/HybridCore/Native.cpp
  lib/HybridCore/Rules.cpp
  "${A2MBA_HYBRID_VERIFIED_RULES}"
)
set_target_properties(A2MBAHybridCore PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(A2MBAHybridCore PUBLIC cxx_std_20)
target_include_directories(A2MBAHybridCore
  PUBLIC "${PROJECT_SOURCE_DIR}/include"
  PRIVATE
    "${A2MBA_HYBRID_SOURCE_DIR}"
    "${A2MBA_HYBRID_GENERATED_DIR}"
)
if(MSVC)
  target_compile_options(A2MBAHybridCore PRIVATE /W4)
else()
  target_compile_options(A2MBAHybridCore PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
  )
endif()

# Keep the plugin and its native self-test under the same sanitizer contract.
function(a2mba_enable_sanitizers target_name)
  if(NOT A2MBA_ENABLE_SANITIZERS)
    return()
  endif()

  if(MSVC OR NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    message(FATAL_ERROR
      "A2MBA_ENABLE_SANITIZERS requires Clang or GCC with the "
      "GNU-style command-line frontend"
    )
  endif()

  target_compile_options(${target_name} PRIVATE
    -fsanitize=address,undefined
    -fno-omit-frame-pointer
  )
  target_link_options(${target_name} PRIVATE -fsanitize=address,undefined)
endfunction()

a2mba_enable_sanitizers(A2MBAHybridCore)

set(A2MBA_PASS_SOURCES
  lib/A2MBA.cpp
  lib/Config.cpp
  lib/Context.cpp
  lib/Eligibility.cpp
  lib/Random.cpp
  lib/Modular.cpp
  lib/AAMBA.cpp
  lib/AGT.cpp
  lib/Hybrid.cpp
  lib/Metadata.cpp
  lib/PassRegistration.cpp
  lib/Statistics.cpp
)

set(A2MBA_STANDALONE_DRIVER OFF)
if(WIN32 AND NOT LLVM_ENABLE_PLUGINS)
  set(A2MBA_STANDALONE_DRIVER ON)
  add_executable(a2mba-opt
    tools/a2mba-opt.cpp
    ${A2MBA_PASS_SOURCES}
  )
  llvm_update_compile_flags(a2mba-opt)
  set_output_directory(a2mba-opt
    BINARY_DIR "${LLVM_RUNTIME_OUTPUT_INTDIR}"
    LIBRARY_DIR "${LLVM_LIBRARY_OUTPUT_INTDIR}"
  )
  target_link_libraries(a2mba-opt PRIVATE LLVMOptDriver)
  set(A2MBA_MAIN_TARGET a2mba-opt)
else()
  add_llvm_pass_plugin(A2MBA
    lib/Plugin.cpp
    ${A2MBA_PASS_SOURCES}
  )
  set(A2MBA_MAIN_TARGET A2MBA)
endif()

separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_options(${A2MBA_MAIN_TARGET} PRIVATE ${LLVM_DEFINITIONS_LIST})
target_compile_features(${A2MBA_MAIN_TARGET} PRIVATE cxx_std_20)
target_compile_definitions(${A2MBA_MAIN_TARGET}
  PRIVATE A2MBA_VERSION_STRING="${PROJECT_VERSION}"
)
target_include_directories(${A2MBA_MAIN_TARGET} PRIVATE
  "${PROJECT_SOURCE_DIR}/include"
  "${PROJECT_SOURCE_DIR}/lib"
)
target_include_directories(${A2MBA_MAIN_TARGET} SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
target_link_libraries(${A2MBA_MAIN_TARGET} PRIVATE A2MBAHybridCore)

if(WIN32)
  target_link_libraries(${A2MBA_MAIN_TARGET} PRIVATE bcrypt)
endif()

a2mba_enable_sanitizers(${A2MBA_MAIN_TARGET})

if(A2MBA_STANDALONE_DRIVER)
  install(TARGETS a2mba-opt RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
else()
  install(TARGETS A2MBA
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/a2mba"
    RUNTIME DESTINATION "${CMAKE_INSTALL_LIBDIR}/a2mba"
  )
endif()
if(WIN32)
  install(PROGRAMS tools/a2mba-clang.py TYPE BIN)
else()
  install(PROGRAMS tools/a2mba-clang.py
    TYPE BIN
    RENAME a2mba-clang
  )
endif()
install(FILES sdk/a2mba.h TYPE INCLUDE)
install(FILES
  LICENSE
  THIRD_PARTY_NOTICES.md
  DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/a2mba"
)
install(DIRECTORY LICENSES
  DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/a2mba"
)
install(FILES
  README.md
  CITATION.cff
  THIRD_PARTY_NOTICES.md
  DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/a2mba"
)
install(FILES .github/SECURITY.md
  DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/a2mba/.github"
)
install(DIRECTORY docs
  DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/a2mba"
  FILES_MATCHING
  PATTERN "*.md"
  PATTERN "*.json"
  PATTERN "*.png"
)

if(A2MBA_BUILD_TESTS)
  enable_testing()
  add_subdirectory(test)
endif()

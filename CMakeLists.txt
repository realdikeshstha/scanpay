cmake_minimum_required(VERSION 3.25)
project(ScanPay VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SCANPAY_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(SCANPAY_SSL "Build with HTTPS support (OpenSSL)" ON)
option(SCANPAY_BUILD_TESTS "Build the CTest suite" ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

if(SCANPAY_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
  add_link_options(-fsanitize=address,undefined)
endif()

# ---------------------------------------------------------------------------
# Third-party dependencies
# ---------------------------------------------------------------------------
include(FetchContent)

set(CROW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CROW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CROW_INSTALL OFF CACHE BOOL "" FORCE)
set(CROW_ENABLE_SSL ${SCANPAY_SSL} CACHE BOOL "" FORCE)

FetchContent_Declare(crow
  GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
  GIT_TAG v1.3.5
  GIT_SHALLOW TRUE
  SYSTEM)

# The QR encoder and doctest are header/source drops; SOURCE_SUBDIR points at a
# directory without a CMakeLists.txt so FetchContent only downloads them.
FetchContent_Declare(qrcodegen
  GIT_REPOSITORY https://github.com/nayuki/QR-Code-generator.git
  GIT_TAG v1.8.0
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR no-cmake)

FetchContent_MakeAvailable(crow qrcodegen)

find_package(PkgConfig REQUIRED)
pkg_check_modules(PQXX REQUIRED IMPORTED_TARGET libpqxx)
pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)
find_package(Threads REQUIRED)

add_library(qrcodegen STATIC ${qrcodegen_SOURCE_DIR}/cpp/qrcodegen.cpp)
target_include_directories(qrcodegen SYSTEM PUBLIC ${qrcodegen_SOURCE_DIR}/cpp)

# ---------------------------------------------------------------------------
# Hardening and warnings for our own code
# ---------------------------------------------------------------------------
set(SCANPAY_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast)
set(SCANPAY_HARDENING -fstack-protector-strong)
if(NOT SCANPAY_SANITIZE)
  list(APPEND SCANPAY_HARDENING $<$<NOT:$<CONFIG:Debug>>:-D_FORTIFY_SOURCE=2>)
endif()

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
add_library(scanpay_core STATIC
  src/config.cpp
  src/crypto.cpp
  src/money.cpp
  src/validate.cpp
  src/security_log.cpp
  src/db/pool.cpp
  src/auth/password.cpp
  src/auth/session.cpp
  src/auth/csrf.cpp
  src/auth/rate_limit.cpp
  src/auth/login.cpp
  src/qr/qr_token.cpp
  src/qr/qr_svg.cpp
  src/payments/authorization.cpp
  src/payments/transfer.cpp
  src/payments/history.cpp
  src/web/cookies.cpp
  src/web/views.cpp
  src/web/guard.cpp
  src/web/services.cpp
  src/web/routes.cpp)
target_include_directories(scanpay_core PUBLIC src)
target_link_libraries(scanpay_core PUBLIC
  Crow::Crow PkgConfig::PQXX PkgConfig::SODIUM qrcodegen Threads::Threads)
target_compile_options(scanpay_core PRIVATE ${SCANPAY_WARNINGS} ${SCANPAY_HARDENING})

add_executable(scanpay src/main.cpp)
target_link_libraries(scanpay PRIVATE scanpay_core)
target_compile_options(scanpay PRIVATE ${SCANPAY_WARNINGS} ${SCANPAY_HARDENING})

add_executable(scanpay_seed tools/seed.cpp)
target_link_libraries(scanpay_seed PRIVATE scanpay_core)
target_compile_options(scanpay_seed PRIVATE ${SCANPAY_WARNINGS} ${SCANPAY_HARDENING})

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(SCANPAY_BUILD_TESTS)
  FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.11
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR no-cmake)
  FetchContent_MakeAvailable(doctest)

  enable_testing()

  add_executable(unit_tests
    tests/unit/main.cpp
    tests/unit/money_test.cpp
    tests/unit/validate_test.cpp
    tests/unit/qr_test.cpp
    tests/unit/crypto_test.cpp
    tests/unit/rate_limit_test.cpp)
  target_include_directories(unit_tests SYSTEM PRIVATE ${doctest_SOURCE_DIR})
  target_link_libraries(unit_tests PRIVATE scanpay_core)
  add_test(NAME unit COMMAND unit_tests)

  # Integration tests need PostgreSQL; they exit with 77 (skipped) when
  # SCANPAY_TEST_DATABASE_URL is not set.
  add_executable(integration_tests
    tests/integration/main.cpp
    tests/integration/fixture.cpp
    tests/integration/payment_test.cpp
    tests/integration/auth_test.cpp
    tests/integration/http_test.cpp)
  target_include_directories(integration_tests SYSTEM PRIVATE ${doctest_SOURCE_DIR})
  target_link_libraries(integration_tests PRIVATE scanpay_core)
  add_test(NAME integration COMMAND integration_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  set_tests_properties(integration PROPERTIES SKIP_RETURN_CODE 77)
endif()

# ---------------------------------------------------------------------------
# Static analysis: `cmake --build build --target cppcheck`
# ---------------------------------------------------------------------------
find_program(CPPCHECK_EXE cppcheck)
if(CPPCHECK_EXE)
  add_custom_target(cppcheck
    COMMAND ${CPPCHECK_EXE}
      --enable=warning,performance,portability,style
      --std=c++20 --language=c++
      --inline-suppr
      --error-exitcode=1
      --suppress=missingIncludeSystem
      --suppress=missingInclude
      --suppress=unusedFunction
      --suppress=useStlAlgorithm
      --suppress=unmatchedSuppression
      -I ${CMAKE_SOURCE_DIR}/src
      ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tools
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running cppcheck"
    VERBATIM)
endif()

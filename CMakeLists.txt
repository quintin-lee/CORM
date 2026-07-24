cmake_minimum_required(VERSION 3.10)
project(corm
    VERSION 0.1.0
    DESCRIPTION "C Database Adapter Layer (GORM-inspired)"
    LANGUAGES C
)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Options ──
option(CORM_WITH_SQLITE3  "Build with SQLite3 support"  ON)
option(CORM_WITH_MYSQL    "Build with MySQL support"    ON)
option(CORM_WITH_POSTGRES "Build with PostgreSQL support" ON)
option(CORM_BUILD_TESTS   "Build tests"                 ON)
option(CORM_BUILD_EXAMPLES "Build examples"             ON)
option(CORM_ENABLE_ASAN    "Enable AddressSanitizer"     OFF)

if(CORM_ENABLE_ASAN)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=address")
    execute_process(COMMAND gcc -print-file-name=libasan.so OUTPUT_VARIABLE ASAN_LIB_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(ASAN_LIB_PATH)
        set(ENV{LD_PRELOAD} "${ASAN_LIB_PATH}")
    endif()
    message(STATUS "AddressSanitizer: ENABLED (${ASAN_LIB_PATH})")
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# ── Find dependencies ──
if(CORM_WITH_SQLITE3)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SQLITE3 sqlite3)
    endif()
    if(NOT SQLITE3_FOUND)
        find_path(SQLITE3_INCLUDE_DIR sqlite3.h)
        find_library(SQLITE3_LIBRARY sqlite3)
        if(SQLITE3_INCLUDE_DIR AND SQLITE3_LIBRARY)
            set(SQLITE3_FOUND TRUE)
        endif()
    endif()
    if(SQLITE3_FOUND)
        add_definitions(-DCORM_HAVE_SQLITE3)
        if(SQLITE3_INCLUDE_DIRS)
            include_directories(${SQLITE3_INCLUDE_DIRS})
        else()
            include_directories(${SQLITE3_INCLUDE_DIR})
        endif()
        if(SQLITE3_LIBRARIES)
            list(APPEND CORM_EXTRA_LIBS ${SQLITE3_LIBRARIES})
        else()
            list(APPEND CORM_EXTRA_LIBS ${SQLITE3_LIBRARY})
        endif()
        message(STATUS "SQLite3: FOUND (${CORM_EXTRA_LIBS})")
    else()
        message(STATUS "SQLite3: NOT FOUND (using stub)")
    endif()
endif()

if(CORM_WITH_MYSQL)
    find_path(MYSQL_INCLUDE_DIR mysql/mysql.h
        PATHS /usr/include/mysql /usr/local/include/mysql /opt/homebrew/include/mysql
    )
    find_library(MYSQL_LIBRARY mysqlclient
        PATHS /usr/lib/mysql /usr/local/lib/mysql /opt/homebrew/lib/mysql
    )
    if(MYSQL_INCLUDE_DIR AND MYSQL_LIBRARY)
        add_definitions(-DCORM_HAVE_MYSQL)
        include_directories(${MYSQL_INCLUDE_DIR})
        set(CORM_EXTRA_LIBS ${CORM_EXTRA_LIBS} ${MYSQL_LIBRARY})
        message(STATUS "MySQL: FOUND")
    else()
        message(STATUS "MySQL: NOT FOUND (using stub)")
    endif()
endif()

if(CORM_WITH_POSTGRES)
    find_package(PostgreSQL QUIET)
    if(PostgreSQL_FOUND)
        add_definitions(-DCORM_HAVE_POSTGRES)
        include_directories(${PostgreSQL_INCLUDE_DIRS})
        set(CORM_EXTRA_LIBS ${CORM_EXTRA_LIBS} ${PostgreSQL_LIBRARIES})
        message(STATUS "PostgreSQL: FOUND")
    else()
        message(STATUS "PostgreSQL: NOT FOUND (using stub)")
    endif()
endif()

# ── Formatting ──
find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    file(GLOB_RECURSE CORM_FORMAT_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/include/corm/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.c
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/*.c
    )

    add_custom_target(format
        COMMAND ${CLANG_FORMAT} -i --style=file ${CORM_FORMAT_SOURCES}
        COMMENT "Formatting all source files with clang-format"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_custom_target(check-format
        COMMAND ${CLANG_FORMAT} --dry-run -Werror --style=file ${CORM_FORMAT_SOURCES}
        COMMENT "Checking source file formatting with clang-format"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()

# ── Sources ──
set(CORM_SOURCES
    src/corm.c
    src/model.c
    src/query.c
    src/result.c
    src/migration.c
    src/builder.c
    src/dialect.c
    src/backend/backend.c
    src/backend/sqlite3.c
    src/backend/mysql.c
    src/backend/postgres.c
    src/internal/stmt_cache.c
    src/internal/pool.c
)

# ── Library ──
add_library(corm STATIC ${CORM_SOURCES})
if(CORM_EXTRA_LIBS)
    target_link_libraries(corm PUBLIC ${CORM_EXTRA_LIBS})
endif()
if(CLANG_FORMAT)
    add_dependencies(corm format)
endif()

# ── Format else (clang-format not found) ──
if(NOT CLANG_FORMAT)
    add_custom_target(format
        COMMAND ${CMAKE_COMMAND} -E echo "clang-format not found -- skipping"
    )
    add_custom_target(check-format
        COMMAND ${CMAKE_COMMAND} -E echo "clang-format not found -- skipping"
    )
endif()

# ── Tests ──
if(CORM_BUILD_TESTS)
    enable_testing()

    # For tests, we need sqlite3 at minimum
    if(CORM_WITH_SQLITE3 AND SQLITE3_FOUND)
        add_executable(test_core tests/test_core.c)
        target_link_libraries(test_core corm ${CORM_EXTRA_LIBS})
        add_test(NAME test_core COMMAND test_core)

        add_executable(test_model tests/test_model.c)
        target_link_libraries(test_model corm ${CORM_EXTRA_LIBS})
        add_test(NAME test_model COMMAND test_model)

        add_executable(test_query tests/test_query.c)
        target_link_libraries(test_query corm ${CORM_EXTRA_LIBS})
        add_test(NAME test_query COMMAND test_query)

        add_executable(test_sqlite tests/test_sqlite.c)
        target_link_libraries(test_sqlite corm ${CORM_EXTRA_LIBS})
        add_test(NAME test_sqlite COMMAND test_sqlite)

        add_executable(test_backend_drivers tests/test_backend_drivers.c)
        target_link_libraries(test_backend_drivers corm ${CORM_EXTRA_LIBS})
        add_test(NAME test_backend_drivers COMMAND test_backend_drivers)

        add_executable(test_pool tests/test_pool.c)
        target_link_libraries(test_pool corm ${CORM_EXTRA_LIBS} pthread)
        add_test(NAME test_pool COMMAND test_pool)

        add_executable(test_migration tests/test_migration.c)
        target_link_libraries(test_migration corm ${CORM_EXTRA_LIBS})
        add_test(NAME test_migration COMMAND test_migration)

        add_executable(test_hooks tests/test_hooks.c)
        target_link_libraries(test_hooks corm ${CORM_EXTRA_LIBS})
        add_test(NAME test_hooks COMMAND test_hooks)

        add_executable(test_full_hooks tests/test_full_hooks.c)
        target_link_libraries(test_full_hooks corm ${CORM_EXTRA_LIBS})
        add_test(NAME test_full_hooks COMMAND test_full_hooks)

        add_executable(test_predicates tests/test_predicates.c)
        target_link_libraries(test_predicates corm ${CORM_EXTRA_LIBS})
        add_test(NAME test_predicates COMMAND test_predicates)

        add_executable(test_preload tests/test_preload.c)
        target_link_libraries(test_preload corm ${CORM_EXTRA_LIBS})
        add_test(NAME test_preload COMMAND test_preload)

        add_executable(test_stmt_cache tests/test_stmt_cache.c src/internal/stmt_cache.c)
        target_link_libraries(test_stmt_cache corm ${CORM_EXTRA_LIBS})
        add_test(NAME test_stmt_cache COMMAND test_stmt_cache)

        add_executable(test_soft_delete tests/test_soft_delete.c)
        target_link_libraries(test_soft_delete corm ${CORM_EXTRA_LIBS})
        add_test(NAME test_soft_delete COMMAND test_soft_delete)

        add_executable(test_logger tests/test_logger.c)
        target_link_libraries(test_logger corm ${CORM_EXTRA_LIBS})
        add_test(NAME test_logger COMMAND test_logger)

        add_executable(test_corm_api tests/test_corm_api.c)
        target_link_libraries(test_corm_api corm ${CORM_EXTRA_LIBS})
        add_test(NAME test_corm_api COMMAND test_corm_api)
    endif()
endif()

# ── Examples ──
if(CORM_BUILD_EXAMPLES)
    add_executable(basic examples/basic.c)
    target_link_libraries(basic corm ${CORM_EXTRA_LIBS})
endif()

# ── Installation ──
include(CMakePackageConfigHelpers)

configure_file(corm.pc.in corm.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/corm.pc DESTINATION lib/pkgconfig)

install(TARGETS corm EXPORT cormTargets DESTINATION lib)
install(DIRECTORY include/corm DESTINATION include)

install(EXPORT cormTargets
    FILE cormTargets.cmake
    DESTINATION lib/cmake/corm
)

configure_package_config_file(cormConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/cormConfig.cmake
    INSTALL_DESTINATION lib/cmake/corm
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/cormConfig.cmake DESTINATION lib/cmake/corm)

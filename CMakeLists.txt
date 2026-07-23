cmake_minimum_required(VERSION 3.10)
project(corm
    VERSION 0.1.0
    DESCRIPTION "C Database Adapter Layer (GORM-inspired)"
    LANGUAGES C
)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ── Options ──
option(CORM_WITH_SQLITE3  "Build with SQLite3 support"  ON)
option(CORM_WITH_MYSQL    "Build with MySQL support"    ON)
option(CORM_WITH_POSTGRES "Build with PostgreSQL support" ON)
option(CORM_BUILD_TESTS   "Build tests"                 ON)
option(CORM_BUILD_EXAMPLES "Build examples"             ON)

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
)

# ── Library ──
add_library(corm STATIC ${CORM_SOURCES})
if(CORM_EXTRA_LIBS)
    target_link_libraries(corm PUBLIC ${CORM_EXTRA_LIBS})
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

        add_executable(test_pool tests/test_pool.c src/internal/pool.c)
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
    endif()
endif()

# ── Examples ──
if(CORM_BUILD_EXAMPLES)
    add_executable(basic examples/basic.c)
    target_link_libraries(basic corm ${CORM_EXTRA_LIBS})
endif()

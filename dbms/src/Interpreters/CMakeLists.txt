
if (CMAKE_SYSTEM MATCHES "FreeBSD")
	set (PATH_SHARE "/usr/local/share" CACHE STRING "")
else ()
	set (PATH_SHARE "/usr/share" CACHE STRING "")
endif ()

if (INTERNAL_COMPILER_SAME)
	set (INTERNAL_COMPILER_EXECUTABLE "${CMAKE_CXX_COMPILER}" CACHE STRING "")
else ()
	set (INTERNAL_COMPILER_EXECUTABLE "${PATH_SHARE}/clickhouse/bin/clang" CACHE STRING "")
endif ()

set (INTERNAL_COMPILER_NO_WARNING OFF CACHE BOOL "")

#original string:
# -x c++ -std=gnu++1y -O3 -g -Wall -Werror -Wnon-virtual-dtor -march=native -msse4 -mpopcnt -D NDEBUG -D_GLIBCXX_USE_CXX11_ABI=0 -shared -fPIC -fvisibility=hidden -fno-implement-inlines
set (INTERNAL_COMPILER_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_RELEASE} -x c++ -march=native -shared -fPIC -fvisibility=hidden -fno-implement-inlines " CACHE STRING "")
string(REPLACE "-no-pie" "" INTERNAL_COMPILER_FLAGS ${INTERNAL_COMPILER_FLAGS})
if (INTERNAL_COMPILER_NO_WARNING)
	string (REPLACE "-Wall" "" INTERNAL_COMPILER_FLAGS ${INTERNAL_COMPILER_FLAGS})
	string (REPLACE "-Werror" "" INTERNAL_COMPILER_FLAGS ${INTERNAL_COMPILER_FLAGS})
endif ()

set (INTERNAL_COMPILER_HEADERS "${PATH_SHARE}/clickhouse/headers" CACHE STRING "")
set (INTERNAL_COMPILER_HEADERS_ROOT "${INTERNAL_COMPILER_HEADERS}" CACHE STRING "")
set (INTERNAL_COMPILER_CUSTOM_ROOT ON CACHE BOOL "")
list(GET Poco_INCLUDE_DIRS 0 Poco_Foundation_INCLUDE_DIR)
list(GET Poco_INCLUDE_DIRS 1 Poco_Util_INCLUDE_DIR)

string (REPLACE ${ClickHouse_SOURCE_DIR} ${INTERNAL_COMPILER_HEADERS} INTERNAL_DOUBLE_CONVERSION_INCLUDE_DIR ${DOUBLE_CONVERSION_INCLUDE_DIR})
string (REPLACE ${ClickHouse_SOURCE_DIR} ${INTERNAL_COMPILER_HEADERS} INTERNAL_Boost_INCLUDE_DIRS ${Boost_INCLUDE_DIRS})
string (REPLACE ${ClickHouse_SOURCE_DIR} ${INTERNAL_COMPILER_HEADERS} INTERNAL_Poco_Foundation_INCLUDE_DIR ${Poco_Foundation_INCLUDE_DIR})
string (REPLACE ${ClickHouse_SOURCE_DIR} ${INTERNAL_COMPILER_HEADERS} INTERNAL_Poco_Util_INCLUDE_DIR ${Poco_Util_INCLUDE_DIR})

message (STATUS "Using internal compiler: headers=${INTERNAL_COMPILER_HEADERS} : ${INTERNAL_COMPILER_EXECUTABLE} ${INTERNAL_COMPILER_FLAGS}")

set (CONFIG_COMPILE ${ClickHouse_BINARY_DIR}/dbms/include/DB/Interpreters/config_compile.h)
configure_file (${ClickHouse_SOURCE_DIR}/dbms/include/DB/Interpreters/config_compile.h.in ${CONFIG_COMPILE})

if (ENABLE_TESTS)
	add_subdirectory (tests)
endif (ENABLE_TESTS)

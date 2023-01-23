# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2021 NKI/AVL, Netherlands Cancer Institute

# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:

# 1. Redistributions of source code must retain the above copyright notice, this
#    list of conditions and the following disclaimer
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.

# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

cmake_minimum_required(VERSION 3.15)

# set the project name
project(pdb-redo-session-manager VERSION 1.1.0 LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GNUInstallDirs)
include(CheckFunctionExists)
include(CheckIncludeFiles)
include(CheckLibraryExists)
include(CMakePackageConfigHelpers)
include(Dart)
include(FindFilesystem)
include(FindPkgConfig)
include(GenerateExportHeader)

set(CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Filesystem REQUIRED)

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers")
elseif(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
endif()

if(MSVC)
    # make msvc standards compliant...
    add_compile_options(/permissive-)

	macro(get_WIN32_WINNT version)
		if (WIN32 AND CMAKE_SYSTEM_VERSION)
			set(ver ${CMAKE_SYSTEM_VERSION})
			string(REPLACE "." "" ver ${ver})
			string(REGEX REPLACE "([0-9])" "0\\1" ver ${ver})

			set(${version} "0x${ver}")
		endif()
	endmacro()

	get_WIN32_WINNT(ver)
	add_definitions(-D_WIN32_WINNT=${ver})
endif()

# Create a revision file, containing the current git version info, if any
include(VersionString)
write_version_header(${PROJECT_SOURCE_DIR}/src)

# Optionally use mrc to create resources
find_package(Mrc QUIET)

if(MRC_FOUND)
	option(USE_RSRC "Use mrc to create resources" ON)
else()
	message(STATUS "Not using resources since mrc was not found")
endif()

if(USE_RSRC)
	set(USE_RSRC 1)
	add_compile_definitions(WEBAPP_USES_RESOURCES)
	mrc_write_header(${PROJECT_SOURCE_DIR}/src/mrsrc.hpp)
endif()

# libraries

set(CMAKE_THREAD_PREFER_PTHREAD)
set(THREADS_PREFER_PTHREAD_FLAG)
find_package(Threads)

find_package(LibArchive REQUIRED)

set(ENV{PKG_CONFIG_PATH} "$ENV{HOME}/.local/lib/pkgconfig/")
pkg_check_modules(PKG_PQ libpqxx>=7.2 libpq IMPORTED_TARGET REQUIRED)

find_program(YARN yarn REQUIRED)

find_package(zeep 6 REQUIRED)
find_package(libmcfp REQUIRED)
find_package(gxrio REQUIRED)

add_executable(prsmd
	${PROJECT_SOURCE_DIR}/src/data-service.cpp
	${PROJECT_SOURCE_DIR}/src/data-service.hpp
	${PROJECT_SOURCE_DIR}/src/run-service.cpp
	${PROJECT_SOURCE_DIR}/src/run-service.hpp
	${PROJECT_SOURCE_DIR}/src/user-service.cpp
	${PROJECT_SOURCE_DIR}/src/user-service.hpp
	${PROJECT_SOURCE_DIR}/src/prsm-db-connection.hpp
	${PROJECT_SOURCE_DIR}/src/prsm-db-connection.cpp
	${PROJECT_SOURCE_DIR}/src/zip-support.hpp
	${PROJECT_SOURCE_DIR}/src/prsmd.cpp)

# yarn rules for javascripts
	
set(webpack_input
	${PROJECT_SOURCE_DIR}/webapp/pdb-redo-bootstrap.scss
	${PROJECT_SOURCE_DIR}/webapp/pdb-redo-result.scss
	${PROJECT_SOURCE_DIR}/webapp/admin.js
	${PROJECT_SOURCE_DIR}/webapp/boxplot.js
	${PROJECT_SOURCE_DIR}/webapp/index.js
	${PROJECT_SOURCE_DIR}/webapp/login.js
	${PROJECT_SOURCE_DIR}/webapp/pdb-redo-result.js
	${PROJECT_SOURCE_DIR}/webapp/pdb-redo-result-loader.js
	${PROJECT_SOURCE_DIR}/webapp/ramaplot.js
	${PROJECT_SOURCE_DIR}/webapp/register.js
	${PROJECT_SOURCE_DIR}/webapp/request.js
	${PROJECT_SOURCE_DIR}/webapp/test-api.js
	${PROJECT_SOURCE_DIR}/webpack.config.js
)

set(webpack_output ${PROJECT_SOURCE_DIR}/docroot/scripts/index.js)

add_custom_command(
	OUTPUT ${webpack_output}
	BYPRODUCTS 
	DEPENDS ${webpack_input}
	COMMAND ${YARN} run $<IF:$<CONFIG:Debug>,build,build-production>
	WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

add_custom_target(webpack DEPENDS ${webpack_output})

add_dependencies(prsmd webpack)

if(USE_RSRC)
	mrc_target_resources(prsmd ${PROJECT_SOURCE_DIR}/docroot/)
endif()

target_include_directories(prsmd PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR})
target_link_libraries(prsmd zeep::zeep LibArchive::LibArchive libmcfp::libmcfp gxrio::gxrio ${REQUIRED_LIBRARIES} PkgConfig::PKG_PQ)

install(TARGETS prsmd
    RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR}
)

# # manual

# install(FILES doc/prsmd.1 DESTINATION ${CMAKE_INSTALL_DATADIR}/man/man1)

# manual

# install(FILES doc/prsmd.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)

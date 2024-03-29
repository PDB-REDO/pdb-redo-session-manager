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
project(pdb-redo-session-manager VERSION 2.0.1 LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GNUInstallDirs)
include(CheckFunctionExists)
include(CheckIncludeFiles)
include(CheckLibraryExists)
include(CMakePackageConfigHelpers)
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

if(NOT MRC_FOUND)
	message(FATAL_ERROR "Cannot use resources since mrc was not found")
endif()

add_compile_definitions(WEBAPP_USES_RESOURCES)
mrc_write_header(${PROJECT_SOURCE_DIR}/src/mrsrc.hpp)

# libraries

set(CMAKE_THREAD_PREFER_PTHREAD)
set(THREADS_PREFER_PTHREAD_FLAG)
find_package(Threads)

find_package(LibArchive REQUIRED)
find_package(libpqxx 7.8 REQUIRED)

find_program(YARN yarn REQUIRED)

find_package(zeep 6.0.8 REQUIRED)
find_package(libmcfp REQUIRED)
find_package(gxrio REQUIRED)

add_subdirectory(mailio EXCLUDE_FROM_ALL)

add_executable(prsmd
	${CMAKE_CURRENT_SOURCE_DIR}/src/api-controller.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/api-controller.hpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/data-service.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/data-service.hpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/https-client.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/https-client.hpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/run-service.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/run-service.hpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/user-service.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/user-service.hpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/token-service.hpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/token-service.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/prsm-db-connection.hpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/prsm-db-connection.cpp

	${CMAKE_CURRENT_SOURCE_DIR}/src/zip-support.hpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/prsmd.cpp)

# yarn rules for javascripts
	
set(webpack_input
	${CMAKE_CURRENT_SOURCE_DIR}/webapp/pdb-redo-style.scss
	${CMAKE_CURRENT_SOURCE_DIR}/webapp/web-component-style.scss

	${CMAKE_CURRENT_SOURCE_DIR}/webapp/admin.js
	${CMAKE_CURRENT_SOURCE_DIR}/webapp/boxplot.js
	${CMAKE_CURRENT_SOURCE_DIR}/webapp/dialog.js
	${CMAKE_CURRENT_SOURCE_DIR}/webapp/index.js
	${CMAKE_CURRENT_SOURCE_DIR}/webapp/inline-entry.js
	${CMAKE_CURRENT_SOURCE_DIR}/webapp/jobs.js
	${CMAKE_CURRENT_SOURCE_DIR}/webapp/pdb-redo-result.js
	${CMAKE_CURRENT_SOURCE_DIR}/webapp/pdb-redo-result-loader.js
	${CMAKE_CURRENT_SOURCE_DIR}/webapp/ramaplot.js
	${CMAKE_CURRENT_SOURCE_DIR}/webapp/tokens.js
	${CMAKE_CURRENT_SOURCE_DIR}/webpack.config.js
)

set(webpack_output ${CMAKE_CURRENT_SOURCE_DIR}/docroot/scripts/index.js)

add_custom_command(OUTPUT ${webpack_output}
	DEPENDS ${webpack_input}
	COMMAND ${YARN} run $<IF:$<CONFIG:Debug>,build,build-production>
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	VERBATIM)

add_custom_target(generate_webpack DEPENDS ${webpack_output})

add_dependencies(prsmd generate_webpack)

mrc_target_resources(prsmd
	${PROJECT_SOURCE_DIR}/docroot/
	${PROJECT_SOURCE_DIR}/rsrc/reset-password-mail.txt
	${PROJECT_SOURCE_DIR}/api/)

target_include_directories(prsmd PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR})
target_link_libraries(prsmd mailio zeep::zeep LibArchive::LibArchive libmcfp::libmcfp gxrio::gxrio libpqxx::pqxx)

install(TARGETS prsmd
    RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR}
)

# # manual

# install(FILES doc/prsmd.1 DESTINATION ${CMAKE_INSTALL_DATADIR}/man/man1)

# manual

# install(FILES doc/prsmd.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)

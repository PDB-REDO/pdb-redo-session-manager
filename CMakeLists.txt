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

cmake_minimum_required(VERSION 3.31)

# set the project name
project(pdb-redo-session-manager VERSION 2.1.0 LANGUAGES CXX)

cmake_policy(SET CMP0167 NEW)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GNUInstallDirs)

include(CPM)

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers")
elseif(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
endif()

# Create a revision file, containing the current git version info, if any
include(VersionString)
write_version_header(${PROJECT_SOURCE_DIR}/src)

# Optionally use mrc to create resources
find_package(mrc REQUIRED)
mrc_write_header(${PROJECT_SOURCE_DIR}/src/mrsrc.hpp)

# libraries

set(BUILD_SHARED_LIBS OFF)

set(CMAKE_THREAD_PREFER_PTHREAD)
set(THREADS_PREFER_PTHREAD_FLAG)
find_package(Threads)

find_package(nlohmann_json REQUIRED)
find_package(LibArchive REQUIRED)

find_package(Boost 1.74 REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(mrc REQUIRED)

CPMAddPackage(NAME libpqxx
    GIT_REPOSITORY https://github.com/jtv/libpqxx.git
    GIT_TAG 7.10.1
    EXCLUDE_FROM_ALL YES)
	
CPMAddPackage(
	NAME mcfp
	GIT_REPOSITORY https://forge.hekkelman.net/maarten/mcfp.git
	GIT_TAG v1.4.2
	EXCLUDE_FROM_ALL YES
	OPTIONS
	"BUILD_TESTING NO")

CPMAddPackage(
	NAME gxrio
	GIT_REPOSITORY https://forge.hekkelman.net/maarten/gxrio.git
	GIT_TAG v1.0.2
	EXCLUDE_FROM_ALL YES
	OPTIONS
	"BUILD_TESTING NO")
CPMAddPackage(NAME mailio
	GIT_REPOSITORY "https://github.com/karastojko/mailio.git"
	GIT_TAG ca9b585
	EXCLUDE_FROM_ALL YES
	OPTIONS
	"MAILIO_BUILD_DOCUMENTATION NO"
	"MAILIO_BUILD_EXAMPLES NO"
	"MAILIO_BUILD_TESTS NO")
CPMAddPackage(NAME zeep
	GIT_REPOSITORY "https://forge.hekkelman.net/maarten/zeep.git"
	GIT_TAG 875eac2
	EXCLUDE_FROM_ALL YES)

find_program(YARN yarn REQUIRED)

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

target_compile_definitions(prsmd PUBLIC WEBAPP_USES_RESOURCES=1)
target_include_directories(prsmd PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR})
target_link_libraries(prsmd mailio zeep::zeep LibArchive::LibArchive mcfp::mcfp gxrio::gxrio libpqxx::pqxx)

install(TARGETS prsmd DESTINATION ${CMAKE_INSTALL_SBINDIR})

set(PRSMD_EXE "${CMAKE_INSTALL_FULL_SBINDIR}/prsmd")
set(PRSMD_PID "/var/run/prsmd")

configure_file(prsmd.service.in ${CMAKE_CURRENT_BINARY_DIR}/prsmd.service @ONLY)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/prsmd.service" DESTINATION "/etc/systemd/system")

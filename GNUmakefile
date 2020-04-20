# Makefile for example app using libzeep
#
# Copyright Maarten L. Hekkelman, UMC St. Radboud 2008-2013.
#        Copyright Maarten L. Hekkelman, 2014-2019
# Distributed under the Boost Software License, Version 1.0.
#    (See accompanying file LICENSE_1_0.txt or copy at
#          http://www.boost.org/LICENSE_1_0.txt)
#
# Use the make.config file in the uplevel directory to
# change the settings for this build

firstTarget: all

# installation prefix
PREFIX              ?= /usr/local

PACKAGES			+= libpqxx libcrypto++

DEFINES				+= WEBAPP_USES_RESOURCES

# main build variables
CXX                 = clang++
CXXFLAGS            += $(BOOST_INC_DIR:%=-I%) -I. -pthread -std=c++17
CXXFLAGS            += -Wall -g
LD                  ?= ld
LDFLAGS				= -g

SVN					= $(shell which svn)
MRC					= $(shell which mrc)

# Use the DEBUG flag to build debug versions of the code
DEBUG               = 0

-include make.config

ifneq ($(PACKAGES),)
CXXFLAGS			+= $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --cflags $(PACKAGES))
LDFLAGS				+= $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --libs $(PACKAGES) --static )
endif

ZEEP_DIR			?= ../libzeep/

ZEEP_INC_DIR		= $(ZEEP_DIR:%=%/include)
ZEEP_LIB_DIR		= $(ZEEP_DIR:%=%/lib)

ZEEP_LIBS			= webapp rest http el xml generic
ZEEP_LIBS			:= $(ZEEP_LIBS:%=zeep-%)

BOOST_INC_DIR       = $(BOOST:%=%/include)
BOOST_LIB_DIR       = $(BOOST:%=%/lib)

BOOST_LIBS          = system thread regex random program_options date_time locale
BOOST_LIBS          := $(BOOST_LIBS:%=boost_%$(BOOST_LIB_SUFFIX))

LIBS                := $(ZEEP_LIBS) $(BOOST_LIBS) stdc++ stdc++fs m pthread $(LIBS)
LDFLAGS             += $(ZEEP_LIB_DIR:%=-L%) $(BOOST_LIB_DIR:%=-L%) $(LIBS:%=-l%) -g

ifeq "$(DEBUG)" "1"
DEFINES				+= DEBUG
else
DEFINES				+= NDEBUG
CXXFLAGS			+= -O2
endif

# targets

VPATH += src

CXXFLAGS			+= $(ZEEP_INC_DIR:%=-I%) $(BOOST_INC_DIR:%=-I%) $(DEFINES:%=-D%)
LDFLAGS				+= 

OBJDIR = obj
ifeq "$(DEBUG)" "1"
	OBJDIR	:= $(OBJDIR).dbg
endif

$(OBJDIR):
	mkdir -p $(OBJDIR)

APPLICATION = prsmd

OBJECTS = \
	$(APPLICATION:%=$(OBJDIR)/%.o) \
	$(APPLICATION:%=$(OBJDIR)/%_rsrc.o) \
	$(OBJDIR)/run-service.o
	
-include $(OBJECTS:%.o=%.d)

$(OBJECTS:.o=.d):

$(OBJDIR)/%.o: %.cpp | src/mrsrc.h $(OBJDIR)
	@ echo ">>" $<
	@ $(CXX) -MD -c -o $@ $< $(CFLAGS) $(CXXFLAGS)

src/mrsrc.h:
	$(MRC) --header > $@

all: $(APPLICATION)
.PHONY: all

REVISION = $(shell LANG=C $(SVN) info | tr -d '\n' | sed -e's/.*Revision: \([[:digit:]]*\).*/\1/' )
REVISION_FILE = version-info-$(REVISION).txt

$(REVISION_FILE):
	LANG=C $(SVN) info > $@

rsrc/version.txt: $(REVISION_FILE) | rsrc
	cp $? $@

rsrc:
	mkdir -p $@

RSRC = rsrc/version.txt docroot/ 

# yarn rules
SCRIPTS = $(shell find webapp -name '*.js')
WEBAPP_FILES = $(SCRIPTS)
SCRIPT_FILES = $(SCRIPTS:webapp/%.js=docroot/scripts/%.js)

ifneq ($(DEBUG),1)
WEBPACK_OPTIONS = --env.PRODUCTIE
endif

$(subst .js,%js,$(SCRIPT_FILES)): $(subst .js,%js,$(WEBAPP_FILES))
	yarn webpack $(WEBPACK_OPTIONS)

$(OBJDIR)/$(APPLICATION)_rsrc.o: $(RSRC) $(SCRIPT_FILES) src/mrsrc.h
	$(MRC) -o $@ $(RSRC) --verbose

$(APPLICATION): %: $(OBJECTS)
	@ echo '->' $@
	@ $(CXX) -o $@ $^ $(LDFLAGS)

clean:
	rm -rf $(OBJDIR)/* $(APPLICATION)

dist-clean: clean

FORCE:


help:
	echo $(REVISION)
########################################################################
# Makefile for Nanotech Construction Kit, an interactive molecular
# dynamics simulation.
# Copyright (c) 2008-2025 Oliver Kreylos
#
# This file is part of the Nanotech Construction Kit.
# 
# The Nanotech Construction Kit is free software; you can redistribute
# it and/or modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2 of the
# License, or (at your option) any later version.
# 
# The Nanotech Construction Kit is distributed in the hope that it will
# be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
# of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with the Nanotech Construction Kit; if not, write to the Free
# Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
# 02111-1307 USA
########################################################################

# Directory containing the Vrui build system. The directory below
# matches the default Vrui installation; if Vrui's installation
# directory was changed during Vrui's installation, the directory below
# must be adapted.
VRUI_MAKEDIR = /usr/local/share/Vrui-13.1/make

# Base installation directory for the Nanotech Construction Kit. If this
# is set to the default of $(PROJECT_ROOT), the Nanotech Construction
# Kit does not have to be installed to be run. Created executables,
# configuration files, and resources will be installed in the bin, etc,
# and share directories under the given base directory, respectively.
# Important note: Do not use ~ as an abbreviation for the user's home
# directory here; use $(HOME) instead.
INSTALLDIR = $(PROJECT_ROOT)

########################################################################
# Everything below here should not have to be changed
########################################################################

PROJECT_NAME = NCK
PROJECT_DISPLAYNAME = Nanotech Construction Kit

# Version number for installation subdirectories. This is used to keep
# subsequent release versions of the Nanotech Construction Kit from
# clobbering each other. The value should be identical to the
# major.minor version number found in VERSION in the root package
# directory.
PROJECT_MAJOR = 2
PROJECT_MINOR = 9

# Include definitions for the system environment and system-provided
# packages
include $(VRUI_MAKEDIR)/SystemDefinitions
include $(VRUI_MAKEDIR)/Packages.System
include $(VRUI_MAKEDIR)/Configuration.Vrui
include $(VRUI_MAKEDIR)/Packages.Vrui
include $(VRUI_MAKEDIR)/Configuration.Collaboration
include $(VRUI_MAKEDIR)/Packages.Collaboration

########################################################################
# Specify additional compiler and linker flags
########################################################################

CFLAGS += -Wall -pedantic

########################################################################
# List common packages used by all components of this project
# (Supported packages can be found in $(VRUI_MAKEDIR)/Packages.*)
########################################################################

PACKAGES = MYGEOMETRY MYMATH MYMISC

########################################################################
# Specify all final targets
########################################################################

CONFIGFILES = 
EXECUTABLES = 
COLLABORATIONPLUGINS = 

CONFIGFILES += Config.h

EXECUTABLES += $(EXEDIR)/NanotechConstructionKit \
               $(EXEDIR)/NewNanotechConstructionKit

# Build the Nanotech Construction Kit server-side collaboration plug-in
NCK_NAME = NCK
NCK_VERSION = 2
COLLABORATIONPLUGINS += $(call COLLABORATIONPLUGIN_SERVER_TARGET,NCK)

ALL = $(EXECUTABLES) $(COLLABORATIONPLUGINS)

.PHONY: all
all: $(ALL)

########################################################################
# Pseudo-target to print configuration options and configure the package
########################################################################

.PHONY: config config-invalidate
config: config-invalidate $(DEPDIR)/config

config-invalidate:
	@mkdir -p $(DEPDIR)
	@touch $(DEPDIR)/Configure-Begin

$(DEPDIR)/Configure-Begin:
	@mkdir -p $(DEPDIR)
	@echo "---- $(PROJECT_FULLDISPLAYNAME) configuration options: ----"
	@touch $(DEPDIR)/Configure-Begin

$(DEPDIR)/Configure-Package: $(DEPDIR)/Configure-Begin
	@echo "Collaborative visualization enabled"
	@touch $(DEPDIR)/Configure-Package

$(DEPDIR)/Configure-Install: $(DEPDIR)/Configure-Package
	@echo "---- $(PROJECT_FULLDISPLAYNAME) installation configuration ----"
	@echo "Installation directory: $(INSTALLDIR)"
	@echo "Executable directory: $(EXECUTABLEINSTALLDIR)"
	@echo "Configuration directory: $(ETCINSTALLDIR)"
	@echo "Collaboration plug-in directory: $(COLLABORATIONPLUGINS_LIBDIR)"
	@touch $(DEPDIR)/Configure-Install

$(DEPDIR)/Configure-End: $(DEPDIR)/Configure-Install
	@echo "---- End of $(PROJECT_FULLDISPLAYNAME) configuration options ----"
	@touch $(DEPDIR)/Configure-End

Config.h: | $(DEPDIR)/Configure-End
	@echo "Creating Config.h configuration file"
	@cp Config.h.template Config.h.temp
	@$(call CONFIG_SETSTRINGVAR,Config.h.temp,NCK_CONFIG_ETCDIR,$(ETCINSTALLDIR))
	@if ! diff -qN Config.h.temp Config.h > /dev/null ; then cp Config.h.temp Config.h ; fi
	@rm Config.h.temp

$(DEPDIR)/config: $(DEPDIR)/Configure-End $(CONFIGFILES)
	@touch $(DEPDIR)/config

########################################################################
# Specify other actions to be performed on a `make clean'
########################################################################

.PHONY: extraclean
extraclean:

.PHONY: extrasqueakyclean
extrasqueakyclean:
	-rm $(CONFIGFILES)

# Include basic makefile
include $(VRUI_MAKEDIR)/BasicMakefile

########################################################################
# Specify build rules for executables
########################################################################

#
# Old Nanotech Construction Kit
#

NANOTECHCONSTRUCTIONKIT_SOURCES = StructuralUnit.cpp \
                                  Triangle.cpp \
                                  Tetrahedron.cpp \
                                  Octahedron.cpp \
                                  Sphere.cpp \
                                  Cylinder.cpp \
                                  GhostUnit.cpp \
                                  UnitManager.cpp \
                                  SpaceGridCell.cpp \
                                  SpaceGrid.cpp \
                                  Polyhedron.cpp \
                                  Simulation.cpp \
                                  ReadUnitFile.cpp \
                                  ReadCarFile.cpp \
                                  UnitDragger.cpp \
                                  NanotechConstructionKit.cpp

$(NANOTECHCONSTRUCTIONKIT_SOURCES:%.cpp=$(OBJDIR)/%.o): | $(DEPDIR)/config

$(EXEDIR)/NanotechConstructionKit: PACKAGES += MYVRUI MYGLMOTIF MYGLGEOMETRY MYGLSUPPORT MYGLWRAPPERS MYIO GL
$(EXEDIR)/NanotechConstructionKit: $(NANOTECHCONSTRUCTIONKIT_SOURCES:%.cpp=$(OBJDIR)/%.o)
.PHONY: NanotechConstructionKit
NanotechConstructionKit: $(EXEDIR)/NanotechConstructionKit

#
# New Nanotech Construction Kit stand-alone or client application
#

NEWNANOTECHCONSTRUCTIONKIT_SOURCES = Simulation.cpp \
                                     ClusterSlaveSimulation.cpp \
                                     NCKProtocol.cpp \
                                     NCKClient.cpp \
                                     NewNanotechConstructionKit.cpp

$(NEWNANOTECHCONSTRUCTIONKIT_SOURCES:%.cpp=$(OBJDIR)/%.o): | $(DEPDIR)/config

$(EXEDIR)/NewNanotechConstructionKit: PACKAGES += MYCOLLABORATION2CLIENT MYVRUI MYGLMOTIF MYGLGEOMETRY MYGLSUPPORT MYGLWRAPPERS MYCLUSTER MYIO MYTHREADS MYREALTIME
$(EXEDIR)/NewNanotechConstructionKit: $(NEWNANOTECHCONSTRUCTIONKIT_SOURCES:%.cpp=$(OBJDIR)/%.o)
.PHONY: NewNanotechConstructionKit
NewNanotechConstructionKit: $(EXEDIR)/NewNanotechConstructionKit

#
# New Nanotech Construction Kit server plug-in
#

NCKSERVER_SOURCES = Simulation.cpp \
                    NCKProtocol.cpp \
                    NCKServer.cpp

$(call PLUGINOBJNAMES,$(NCKSERVER_SOURCES)): | $(DEPDIR)/config

$(call COLLABORATIONPLUGIN_SERVER_TARGET,NCK): PACKAGES += MYGEOMETRY MYMATH MYIO MYTHREADS MYREALTIME MYMISC
$(call COLLABORATIONPLUGIN_SERVER_TARGET,NCK): $(call PLUGINOBJNAMES,$(NCKSERVER_SOURCES))
.PHONY: NCKServer
NCKServer: $(call COLLABORATIONPLUGIN_SERVER_TARGET,NCK)

install: $(ALL)
	@echo Installing $(PROJECT_DISPLAYNAME) in $(INSTALLDIR)...
	@install -d $(INSTALLDIR)
	@install -d $(EXECUTABLEINSTALLDIR)
	@install $(EXECUTABLES) $(EXECUTABLEINSTALLDIR)
	@install $(COLLABORATIONPLUGINS) $(COLLABORATIONPLUGINS_LIBDIR)
	@install -d $(ETCINSTALLDIR)
	@install -m u=rw,go=r $(PROJECT_ETCDIR)/NCK.cfg $(ETCINSTALLDIR)


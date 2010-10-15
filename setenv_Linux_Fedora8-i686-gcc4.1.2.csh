#!/bin/tcsh
 
# This file was generated by the script "mk_setenv.csh"
#
# Generation date: Fri Oct 15 11:55:23 EDT 2010
# User: gluex
# Host: ifarml5
# Platform: Linux ifarml5 2.6.23.1-42.fc8PAE #1 SMP Tue Oct 30 13:45:30 EDT 2007 i686 i686 i386 GNU/Linux
# BMS_OSNAME: Linux_Fedora8-i686-gcc4.1.2
 
# Make sure LD_LIBRARY_PATH is set
if ( ! $?LD_LIBRARY_PATH ) then
   setenv LD_LIBRARY_PATH
endif
 
# HALLD
setenv HALLD_HOME /group/halld/Software/builds/sim-recon/sim-recon-2010-10-11
setenv HDDS_HOME /group/halld/Software/builds/hdds/hdds-1.0
setenv BMS_OSNAME Linux_Fedora8-i686-gcc4.1.2
setenv PATH ${HALLD_HOME}/bin/${BMS_OSNAME}:$PATH
 
# JANA
setenv JANA_HOME /group/12gev_phys/builds/jana_0.6.2/Linux_Fedora8-i686-gcc4.1.2
setenv JANA_CALIB_URL file:///group/halld/Software/calib/latest
setenv JANA_GEOMETRY_URL xmlfile://${HDDS_HOME}/main_HDDS.xml
setenv JANA_PLUGIN_PATH ${JANA_HOME}/lib
setenv PATH ${JANA_HOME}/bin:$PATH
 
# ROOT
setenv ROOTSYS /apps/root/PRO/root
setenv LD_LIBRARY_PATH ${ROOTSYS}/lib:$LD_LIBRARY_PATH
setenv PATH ${ROOTSYS}/bin:$PATH
 
# CERNLIB
setenv CERN /apps/cernlib/i386_fc8
setenv CERN_LEVEL 2005
setenv LD_LIBRARY_PATH ${CERN}/${CERN_LEVEL}/lib:$LD_LIBRARY_PATH
setenv PATH ${CERN}/${CERN_LEVEL}/bin:$PATH
 
# Xerces
setenv XERCESCROOT /group/halld/Software/ExternalPackages/xerces-c-src_2_7_0.Linux_Fedora8-i686-gcc4.1.2
setenv LD_LIBRARY_PATH ${XERCESCROOT}/lib:$LD_LIBRARY_PATH
 

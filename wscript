#! /usr/bin/env python
# encoding: utf-8
'''
@file wscript
@brief This is the main Waf based build sytem file for MapTrack

This file is part of Skyrim SE Map Tracker mod (aka MapTrack).

  MapTrack is free software: you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  MapTrack is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with MapTrack. If not, see <http://www.gnu.org/licenses/>.

@endinternal

@ingroup Builds

@details
Waf is Python (2/3) based build system similar to SCons & Make.
@see https://waf.io/book/
'''

import os
import shutil, subprocess

#---------------------------------------------------------------------------------------------------

top = '.'
''' Representing the project directory, relative to this wscript. In general, set to .'''

out = 'out'
''' String representing the build directory. Can be an absolute path too. It is important to be 
able to remove the build directory safely, so it should never be given as . or similar. '''

APPNAME = 'sse-maptrack'
''' Used to specify the generated executable names and the generated distribution archive name. '''

VERSION = open ('VERSION', 'r').readline ().strip ().replace (',', '.') 
''' The version field is used accross the project: for distro tars, for documentation, for file
stamps and etc. It is taken from central file - useful to share accross its users. '''

#---------------------------------------------------------------------------------------------------

def options(opt):
    opt.load('compiler_cxx')

def configure(conf):
    conf.load('compiler_cxx')

    if conf.env['CXX_NAME'] == 'gcc':
        conf.check_cxx (msg="Checking for '-std=c++20'", cxxflags='-std=c++20') 
        conf.env.append_unique('CXXFLAGS', \
                ['-std=c++20', "-O2", "-Wall", "-Wno-parentheses", "-D_UNICODE", "-DUNICODE"])
        conf.env.append_unique ('STLIB', ['stdc++', 'pthread', 'ole32'])
        conf.env.append_unique ('LINKFLAGS', ['-static-libgcc', '-static-libstdc++'])

def build (bld):
    bld.shlib (
        target   = APPNAME, 
        source   = bld.path.ant_glob (["src/*.cpp", "share/utils/*.cpp"]), 
        includes = ['src', 'share'],
        cxxflags = ['-DPLUGIN_TIMESTAMP="'+str(_datetime_now())+'"', '-DCIMGUI_NO_EXPORT',
            '-DPLUGIN_NAME="' + APPNAME + '"'])

def _pack_asset (bld, folder, name):
    shutil.rmtree ("Data", ignore_errors=True)
    shutil.copytree ("assets/"+folder+"/Data", "Data")
    subprocess.Popen (["7z", "a", APPNAME+"-"+name+"-"+VERSION+".7z", 'Data']).communicate ()
    shutil.rmtree ("Data", ignore_errors=True)
    
def pack (bld):
    shutil.rmtree ("Data", ignore_errors=True)
    shutil.copytree ("assets/main/Data", "Data")
    
    dll = APPNAME+".dll"
    root = "data/skse/plugins/"
    shutil.copyfile ("out/"+dll, root+dll)
    subprocess.Popen (["x86_64-w64-mingw32-strip", "-g", root+dll]).communicate ()

    subprocess.Popen (["7z", "a", APPNAME+"-"+VERSION+".7z", 'Data']).communicate ()
    shutil.rmtree ("Data", ignore_errors=True)

    _pack_asset (bld, "optional-map", "map")
    _pack_asset (bld, "optional-icons", "icons")

#---------------------------------------------------------------------------------------------------

def _datetime_now ():
    from datetime import datetime, timedelta, tzinfo
    """ Python 3.2 and less miss timezones."""
    class UTC (tzinfo):
        def utcoffset (self, dt):
            return timedelta (0)
        def tzname (self, dt):
            return "UTC"
        def dst (self, dt):
            return timedelta (0)
    return datetime.now (UTC ()).isoformat ()

#---------------------------------------------------------------------------------------------------

# Used to build Makefile.in

EXTRA_DIST = ha_filesystem.h linereader.h linereader.cc formatinfo.h formatinfo.cc

# By default we are set to compile with debug
#INCLUDES =  -DMYSQL_DYNAMIC_PLUGIN  -DUNIV_MUST_NOT_INLINE -DFORCE_INIT_OF_VARS $(MYSQL_INC)
INCLUDES =  $(ENG_MYSQL_INC) 
libfilesystem_engine_la_CXXFLAGS =  -DMYSQL_DYNAMIC_PLUGIN  -DDBUG_ON -DSAFE_MUTEX -DUNIV_MUST_NOT_INLINE -DEXTRA_DEBUG -DFORCE_INIT_OF_VARS -DSAFE_MUTEX

LIBS = -L/usr/local/lib
LDADD =

noinst_HEADERS = ha_filesystem.h

lib_LTLIBRARIES = libfilesystem_engine.la
libfilesystem_engine_la_SOURCES = ha_filesystem.cc linereader.cc formatinfo.cc
libfilesystem_engine_la_LDFLAGS =	-module

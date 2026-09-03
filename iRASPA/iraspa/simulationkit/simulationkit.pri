INCLUDEPATH += $$PWD

contains(DEFINES,USE_DIRECTX){
   include(directx12/directx12.pri)
  }

SOURCES += \
    $$PWD/forcefieldsets.cpp \
    $$PWD/forcefieldset.cpp \
    $$PWD/forcefieldtype.cpp \
    $$PWD/constants.cpp \
    $$PWD/marchingcubes.cpp \
    $$PWD/skwellsurface.cpp

HEADERS += \
    $$PWD/lookuptable.h \
    $$PWD/simulationkit.h \
    $$PWD/skwellsurface.h \
    $$PWD/forcefieldsets.h \
    $$PWD/forcefieldset.h \
    $$PWD/forcefieldtype.h \
    $$PWD/constants.h \
    $$PWD/marchingcubes.h

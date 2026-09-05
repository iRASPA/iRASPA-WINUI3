INCLUDEPATH += $$PWD

LIBS += -ldxgi -ld3d12 -ld3dcompiler

include(geometry/geometry.pri)

SOURCES += \
    $$PWD/rkfontatlas.cpp \
    $$PWD/directxshader.cpp \
    $$PWD/directxuniformstringliterals.cpp \
    $$PWD/directxbackgroundshader.cpp \
    $$PWD/directxatomsphereshader.cpp \
    $$PWD/directxatomorthographicimpostershader.cpp \
    $$PWD/directxatomperspectiveimpostershader.cpp \
    $$PWD/directxatomshader.cpp \
    $$PWD/directxambientocclusionshadowmapshader.cpp \
    $$PWD/directxbondshader.cpp \
    $$PWD/directxobjectshader.cpp \
    $$PWD/directxunitcellshader.cpp \
    $$PWD/directxlocalaxesshader.cpp \
    $$PWD/directxboundingboxshader.cpp \
    $$PWD/directxtextrenderingshader.cpp \
    $$PWD/directxglobalaxesshader.cpp \
    $$PWD/directxblockingpocketsshader.cpp \
    $$PWD/directxenergysurface.cpp \
    $$PWD/directxgeometricsurface.cpp \
    $$PWD/directxenergyvolumerenderedsurface.cpp \
    $$PWD/directxenergyvolumetransferfunctions.cpp \
    $$PWD/directxpickingshader.cpp \
    $$PWD/directxatomselectionworleynoise3dshader.cpp \
    $$PWD/directxatomselectionstripesshader.cpp \
    $$PWD/directxatomselectionglowshader.cpp \
    $$PWD/directxbondselectionshader.cpp \
    $$PWD/directxselectionshader.cpp \
    $$PWD/directxblurshader.cpp \
    $$PWD/directxcompositeshader.cpp

HEADERS += \
    $$PWD/rkfontatlas.h \
    $$PWD/directxshader.h \
    $$PWD/directxuniformstringliterals.h \
    $$PWD/directxdevicehelpers.h \
    $$PWD/directxbackgroundshader.h \
    $$PWD/directxatomsphereshader.h \
    $$PWD/directxatomorthographicimpostershader.h \
    $$PWD/directxatomperspectiveimpostershader.h \
    $$PWD/directxatomshader.h \
    $$PWD/directxambientocclusionshadowmapshader.h \
    $$PWD/directxbondshader.h \
    $$PWD/directxobjectshader.h \
    $$PWD/directxunitcellshader.h \
    $$PWD/directxlocalaxesshader.h \
    $$PWD/directxboundingboxshader.h \
    $$PWD/directxtextrenderingshader.h \
    $$PWD/directxglobalaxesshader.h \
    $$PWD/directxblockingpocketsshader.h \
    $$PWD/directxenergysurface.h \
    $$PWD/directxgeometricsurface.h \
    $$PWD/directxenergyvolumerenderedsurface.h \
    $$PWD/directxpickingshader.h \
    $$PWD/directxatomselectionworleynoise3dshader.h \
    $$PWD/directxatomselectionstripesshader.h \
    $$PWD/directxatomselectionglowshader.h \
    $$PWD/directxbondselectionshader.h \
    $$PWD/directxselectionshader.h \
    $$PWD/directxblurshader.h \
    $$PWD/directxcompositeshader.h

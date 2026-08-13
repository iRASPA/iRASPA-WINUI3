INCLUDEPATH += $$PWD

LIBS += -ldxgi -ld3d12 -ld3dcompiler

SOURCES += \
    $$PWD/skdx12.cpp \
    $$PWD/skcomputeenergygrid.cpp \
    $$PWD/skcomputeisosurface.cpp \
    $$PWD/skcomputevoidfraction.cpp

HEADERS += \
    $$PWD/skdx12.h \
    $$PWD/skcomputeenergygrid.h \
    $$PWD/skcomputeisosurface.h \
    $$PWD/skcomputevoidfraction.h

# Embedded HLSL string for isosurface compute kernels
OTHER_FILES += $$PWD/skcomputeisosurface_kernel_string.inc \
               $$PWD/skcomputeisosurface_kernels.hlsl.txt

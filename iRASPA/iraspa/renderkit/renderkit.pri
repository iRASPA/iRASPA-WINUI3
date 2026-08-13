INCLUDEPATH += $$PWD


 contains(DEFINES,USE_DIRECTX){
   include(directx12/directx12.pri)
 }


SOURCES += \
    $$PWD/rkcamera.cpp \
    $$PWD/rkglobalaxes.cpp \
    $$PWD/rklight.cpp \
    $$PWD/rklocalaxes.cpp \
    $$PWD/rkrenderkitprotocols.cpp \
    $$PWD/trackball.cpp \
    $$PWD/rkrenderuniforms.cpp


HEADERS += \
    $$PWD/renderkit.h \
    $$PWD/rkcamera.h \
    $$PWD/rkglobalaxes.h \
    $$PWD/rklight.h \
    $$PWD/rklocalaxes.h \
    $$PWD/rkrenderkitprotocols.h \
    $$PWD/trackball.h \
    $$PWD/rkrenderuniforms.h

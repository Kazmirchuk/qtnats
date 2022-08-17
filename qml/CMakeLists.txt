include_directories(${PROJECT_BINARY_DIR})  # to find qtnats_export.h

#my goodness, this thing generates 7 MSVS projects :o
qt_add_qml_module(qmlnatsplugin
    URI NATS
    VERSION 0.1
    PLUGIN_TARGET qmlnatsplugin
    SOURCES qmlnatsplugin.cpp qmlnatsplugin.h
)

target_link_libraries(qmlnatsplugin PRIVATE qtnats)

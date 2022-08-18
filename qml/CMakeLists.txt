
add_library(qmlnatsplugin SHARED qmlnatsplugin.cpp qmlnatsplugin.h)

qt_add_qml_module(qmlnatsplugin
    URI NATS
    # FIXME: the version MUST be set to 1.0
    # otherwise the generated qmlnatsplugin_qmltyperegistrations.cpp registers the NATS module twice, and then loading the plugin fails
    VERSION 1.0
    PLUGIN_TARGET qmlnatsplugin
)

include_directories(${PROJECT_BINARY_DIR})  # to find qtnats_export.h
target_link_libraries(qmlnatsplugin PUBLIC qtnats)

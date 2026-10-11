add_library(obs-phone-camera MODULE
    module.cpp
    locale.cpp
    tr.h
    paired_store.cpp
    paired_store.h
    net_util.cpp
    net_util.h
    nv12_util.cpp
    nv12_util.h
    status_card.cpp
    status_card.h
    qr_window.cpp
    qr_window.h
    decoder.h
    session.cpp
    session.h
    server.cpp
    server.h
    source.cpp
    source.h
    ${CMAKE_SOURCE_DIR}/vendor/qrcodegen.cpp
    ${CMAKE_SOURCE_DIR}/vendor/qrcodegen.hpp
)

target_include_directories(obs-phone-camera PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/vendor)
target_link_libraries(obs-phone-camera PRIVATE ocp_core LibObs::libobs)

set_target_properties(obs-phone-camera PROPERTIES PREFIX "" OUTPUT_NAME "obs-phone-camera")

if(WIN32)
    target_link_libraries(obs-phone-camera PRIVATE ocp_decoder
        mfplat mfuuid ole32 user32 gdi32 ws2_32 shell32 advapi32 iphlpapi)
    target_compile_definitions(obs-phone-camera PRIVATE UNICODE _UNICODE)
    target_link_options(obs-phone-camera PRIVATE
        /EXPORT:obs_module_text /EXPORT:obs_module_get_string)
elseif(APPLE)
    target_sources(obs-phone-camera PRIVATE decoder_mac.mm status_card_mac.mm qr_window_mac.mm)
    target_compile_options(obs-phone-camera PRIVATE -fobjc-arc)
    target_link_libraries(obs-phone-camera PRIVATE
        "-framework VideoToolbox" "-framework CoreMedia" "-framework CoreVideo"
        "-framework AppKit" "-framework Foundation")
endif()

install(TARGETS obs-phone-camera LIBRARY DESTINATION obs-plugins/64bit)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/data/ DESTINATION data/obs-plugins/obs-phone-camera)

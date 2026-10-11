add_executable(ocp_tests
    main.cpp
    test_framework.h
    test_crypto.cpp
    test_json.cpp
    test_control_frame.cpp
    test_media.cpp
    test_pairing.cpp
    test_discovery.cpp
    test_config.cpp
    test_messages.cpp
    test_h264.cpp
)

target_link_libraries(ocp_tests PRIVATE ocp_core)
target_include_directories(ocp_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(WIN32 AND TARGET ocp_decoder)
    target_sources(ocp_tests PRIVATE test_status_card.cpp
        ${CMAKE_SOURCE_DIR}/src/plugin/status_card.cpp
        ${CMAKE_SOURCE_DIR}/vendor/qrcodegen.cpp)
    target_include_directories(ocp_tests PRIVATE ${CMAKE_SOURCE_DIR}/vendor)
    target_link_libraries(ocp_tests PRIVATE gdi32 user32)
    target_sources(ocp_tests PRIVATE test_decoder_win.cpp)
    target_link_libraries(ocp_tests PRIVATE ocp_decoder)
    target_compile_definitions(ocp_tests PRIVATE
        OCP_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/data")
endif()

add_test(NAME ocp_unit_tests COMMAND ocp_tests)

if(WIN32 AND LibObs_FOUND AND TARGET obs-phone-camera)
    add_executable(module_host module_host/main.cpp)
    target_link_libraries(module_host PRIVATE LibObs::libobs ocp_core ws2_32)
    target_compile_definitions(module_host PRIVATE OCP_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/data")

    if(OBS_ROOT)
        set(_ocp_obs_bin "${OBS_ROOT}/bin/64bit")
    else()
        set(_ocp_obs_bin "C:/Program Files/obs-studio/bin/64bit")
    endif()

    add_test(NAME ocp_module_host
        COMMAND module_host
            $<TARGET_FILE:obs-phone-camera>
            "${CMAKE_SOURCE_DIR}/data")
    set_tests_properties(ocp_module_host PROPERTIES
        ENVIRONMENT "PATH=${_ocp_obs_bin};$ENV{PATH}"
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:obs-phone-camera>")
endif()

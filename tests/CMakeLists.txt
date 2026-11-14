add_executable(test_protocol protocol/test_protocol.c ../native/sbbase/src/protocol.c)
target_include_directories(test_protocol PRIVATE ../native/sbbase/include)
add_test(NAME protocol COMMAND test_protocol)
add_executable(test_nowplaying nowplaying/test_nowplaying.c ../native/sbbase/src/nowplaying.c)
target_include_directories(test_nowplaying PRIVATE ../native/sbbase/include)
add_test(NAME nowplaying_unit COMMAND test_nowplaying)
add_executable(test_alarms alarms/test_alarms.c
  ../native/sbbase/src/alarms.c ../native/sbbase/src/nowplaying.c)
target_include_directories(test_alarms PRIVATE ../native/sbbase/include)
add_test(NAME alarms_unit COMMAND test_alarms)
add_executable(test_sleep alarms/test_sleep.c ../native/sbbase/src/sleep.c ../native/sbbase/src/nowplaying.c)
target_include_directories(test_sleep PRIVATE ../native/sbbase/include)
add_test(NAME sleep_unit COMMAND test_sleep)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_library(backup_rename_fault SHARED backup/fault_rename.c)
  target_link_libraries(backup_rename_fault PRIVATE dl)
  add_executable(test_artwork_settings images/test_artwork_settings.c
    ../native/sbwebserver/src/settings.c)
  target_include_directories(test_artwork_settings PRIVATE ../native/sbwebserver/src)
  add_test(NAME artwork_settings_defaults COMMAND test_artwork_settings)
  find_package(Threads REQUIRED)
  add_executable(test_http_trace images/test_http_trace.c)
  target_link_libraries(test_http_trace PRIVATE Threads::Threads)
  add_test(NAME http_trace_native COMMAND test_http_trace)
  add_executable(test_mongoose_clock protocol/test_mongoose_clock.c
    ../native/sbwebserver/third_party/mongoose.c)
  target_include_directories(test_mongoose_clock PRIVATE ../native/sbwebserver/third_party)
  target_compile_definitions(test_mongoose_clock PRIVATE _GNU_SOURCE
    MG_ENABLE_EPOLL=0 MG_ENABLE_POLL=1)
  target_link_libraries(test_mongoose_clock PRIVATE Threads::Threads
    "-Wl,--wrap=clock_gettime" "-Wl,--wrap=gettimeofday")
  add_test(NAME mongoose_clock_fallback COMMAND test_mongoose_clock)
endif()
add_test(NAME squeezeplay_artwork_patch COMMAND sh
  ${CMAKE_SOURCE_DIR}/tests/comet/test_artwork_patch.sh
  ${CMAKE_SOURCE_DIR}/scripts/patch-squeezeplay-artwork.sh)
add_test(NAME storage COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/storage/test_storage.sh $<TARGET_FILE:sb-storage-helper>)
add_test(NAME storage_boothelper COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/storage/test_boothelper.sh ${CMAKE_SOURCE_DIR}/scripts/sbdata-boot.sh)
add_test(NAME extended_storage COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/storage/test_extended_storage.sh
  ${CMAKE_SOURCE_DIR}/scripts/storage-setup.sh
  ${CMAKE_SOURCE_DIR}/scripts/storage-boot.sh
  $<TARGET_FILE:sb-storage-helper>
  ${CMAKE_SOURCE_DIR}/artifacts/sbubifs-authorized.ko
  ${CMAKE_SOURCE_DIR}/artifacts/ubi/sbdata-empty.ubifs)
find_program(LUA_EXECUTABLE NAMES lua lua5.1)
if(LUA_EXECUTABLE)
  add_test(NAME touch_bridge_lua COMMAND ${LUA_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/remote/test_touch_bridge.lua ${CMAKE_SOURCE_DIR})
  add_test(NAME platform_identity COMMAND ${LUA_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_platform.lua ${CMAKE_SOURCE_DIR})
  add_test(NAME alarm_playback_lua COMMAND ${LUA_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/alarms/test_playback.lua ${CMAKE_SOURCE_DIR})
  add_test(NAME firmware_physical_input COMMAND ${LUA_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/firmware/test_physical.lua ${CMAKE_SOURCE_DIR})
  add_test(NAME firmware_bridge_lua COMMAND ${LUA_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/firmware/test_bridge.lua ${CMAKE_SOURCE_DIR})
  add_test(NAME storage_recovery_menu COMMAND ${LUA_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/storage/test_recovery_menu.lua ${CMAKE_SOURCE_DIR})
  add_test(NAME storage_recovery_manager COMMAND ${LUA_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/storage/test_storage_recovery_manager.lua
    ${CMAKE_SOURCE_DIR})
  add_test(NAME artwork_lua COMMAND ${LUA_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/images/test_artwork.lua ${CMAKE_SOURCE_DIR})
  add_test(NAME image_helper_lua COMMAND ${LUA_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/images/test_image_helper.lua ${CMAKE_SOURCE_DIR})
  add_test(NAME installer_compat_lua COMMAND ${LUA_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/images/test_installer_compat.lua ${CMAKE_SOURCE_DIR})
  add_test(NAME http_trace_lua COMMAND ${LUA_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/images/test_http_trace.lua ${CMAKE_SOURCE_DIR})
  add_test(NAME extended_storage_lua_state COMMAND ${LUA_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/storage/test_extended_storage_state.lua
    ${CMAKE_SOURCE_DIR}/applet)
  add_test(NAME server_migration_lua COMMAND ${LUA_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/comet/test_server_migration.lua
    ${CMAKE_SOURCE_DIR})
  add_test(NAME network_address_lua COMMAND ${LUA_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/comet/test_network_address.lua
    ${CMAKE_SOURCE_DIR})
  add_test(NAME playback_fence_lua COMMAND ${LUA_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/music/test_playback_fence.lua
    ${CMAKE_SOURCE_DIR})
endif()

find_program(NODE_EXECUTABLE NAMES node nodejs)
if(NODE_EXECUTABLE)
  add_test(NAME remote_pixel_decode COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/remote/test_pixels.js ${CMAKE_SOURCE_DIR}/native/sbwebserver/web/js/remote.js)
  add_test(NAME process_monitor_frontend COMMAND ${NODE_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/monitor/test_performance_logic.js
    ${CMAKE_SOURCE_DIR}/native/sbwebserver/web/js/performance.js)
endif()
add_test(NAME storage_updater COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/storage/test_updater.sh $<TARGET_FILE:sb-storage-updater>)
add_executable(test_remote_framebuffer remote/test_framebuffer.c ../native/sbwebserver/src/remote_framebuffer.c)
target_include_directories(test_remote_framebuffer PRIVATE ../native/sbwebserver/src)
add_test(NAME remote_framebuffer COMMAND test_remote_framebuffer)
if(NOT WIN32)
  add_test(NAME applet_repository COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/repository/test_repository.sh $<TARGET_FILE:sbwebserver>)
endif()
if(NOT WIN32)
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_FOUND AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT CMAKE_CROSSCOMPILING)
    add_test(NAME nowplaying_metadata COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/nowplaying/test_metadata.py)
  endif()
  if(Python3_Interpreter_FOUND)
    add_test(NAME release_deployment COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/packaging/test_release_deployment.py ${CMAKE_SOURCE_DIR}/packaging/deploy-remote.sh)
    add_test(NAME storage_dynamic_layout COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/storage/test_dynamic_layout.py $<TARGET_FILE:sb-storage-helper>)
    add_test(NAME storage_firmware_recovery COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/storage/test_firmware_recovery.py
      ${CMAKE_SOURCE_DIR}/scripts/storage-setup.sh
      ${CMAKE_SOURCE_DIR}/scripts/storage-boot.sh $<TARGET_FILE:sb-storage-helper>
      ${CMAKE_SOURCE_DIR}/artifacts/sbubifs-authorized.ko)
    find_package(Threads REQUIRED)
    add_executable(test_catalog_merge repository/test_catalog_merge.c
      ../native/sbwebserver/src/repository.c ../native/sbwebserver/third_party/mongoose.c)
    target_include_directories(test_catalog_merge PRIVATE
      ../native/sbwebserver/src ../native/sbwebserver/third_party)
    target_compile_definitions(test_catalog_merge PRIVATE _GNU_SOURCE MG_ENABLE_EPOLL=0 MG_ENABLE_POLL=1)
    target_link_libraries(test_catalog_merge PRIVATE Threads::Threads)
    add_test(NAME catalog_merge COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/repository/test_catalog_merge.py $<TARGET_FILE:test_catalog_merge>)
    get_target_property(CATALOG_WEB_SOURCES sbwebserver SOURCES)
    set(CATALOG_TEST_SOURCES)
    foreach(source IN LISTS CATALOG_WEB_SOURCES)
      list(APPEND CATALOG_TEST_SOURCES "${CMAKE_SOURCE_DIR}/native/sbwebserver/${source}")
    endforeach()
    add_executable(sbwebserver_catalog_test ${CATALOG_TEST_SOURCES})
    target_include_directories(sbwebserver_catalog_test PRIVATE
      ../native/sbwebserver/src ../native/sbwebserver/third_party ../native/sbbase/include)
    target_compile_definitions(sbwebserver_catalog_test PRIVATE _GNU_SOURCE SB_CATALOG_TESTING SB_FIRMWARE_TESTING SB_IMAGE_TESTING
      MG_ENABLE_EPOLL=0 MG_ENABLE_POLL=1 MG_ENABLE_DIRLIST=0 MG_ENABLE_SSI=0)
    find_package(ZLIB REQUIRED)
    target_link_libraries(sbwebserver_catalog_test PRIVATE Threads::Threads ZLIB::ZLIB)
    add_executable(test_firmware_image firmware/image_check.c
      ../native/sbwebserver/src/firmware_image.c ../native/sbwebserver/third_party/mongoose.c)
    target_include_directories(test_firmware_image PRIVATE
      ../native/sbwebserver/src ../native/sbwebserver/third_party)
    target_compile_definitions(test_firmware_image PRIVATE _GNU_SOURCE MG_ENABLE_EPOLL=0 MG_ENABLE_POLL=1)
    target_link_libraries(test_firmware_image PRIVATE Threads::Threads ZLIB::ZLIB)
    add_test(NAME firmware_image COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/firmware/test_image.py $<TARGET_FILE:test_firmware_image>)
    add_test(NAME firmware_http COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/firmware/test_http.py $<TARGET_FILE:sbwebserver_catalog_test>
      ${CMAKE_SOURCE_DIR}/native/sbwebserver/web)
    add_test(NAME firmware_upload COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/firmware/test_upload.py $<TARGET_FILE:sbwebserver_catalog_test>
      ${CMAKE_SOURCE_DIR}/native/sbwebserver/web)
    add_test(NAME firmware_checksum_download COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/firmware/test_download.py $<TARGET_FILE:sbwebserver_catalog_test>
      ${CMAKE_SOURCE_DIR}/native/sbwebserver/web)
    add_test(NAME storage_native_boot COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/storage/test_native_boot.py
      ${CMAKE_SOURCE_DIR}/scripts/storage-boot.sh)
    add_test(NAME catalog_sync COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/repository/test_catalog_sync.py $<TARGET_FILE:sbwebserver_catalog_test>
      ${CMAKE_SOURCE_DIR}/native/sbwebserver/web)
    add_executable(test_public_address images/test_public_address.c)
    add_test(NAME image_public_address COMMAND test_public_address)
    add_test(NAME svg_renderer COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/images/test_renderer.py $<TARGET_FILE:svgpngrenderer>)
    add_test(NAME raster_renderer COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/images/test_raster.py $<TARGET_FILE:svgpngrenderer>)
    add_test(NAME svg_image_http COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/images/test_image_http.py $<TARGET_FILE:sbwebserver>
      $<TARGET_FILE:svgpngrenderer> ${CMAKE_SOURCE_DIR}/native/sbwebserver/web)
    if(TARGET sbproxy)
      find_package(CURL REQUIRED)
      find_package(Threads REQUIRED)
      add_executable(test_tls_diagnostics tls/test_tls_diagnostics.c)
      target_link_libraries(test_tls_diagnostics PRIVATE CURL::libcurl Threads::Threads)
      add_test(NAME proxy_tls_diagnostics COMMAND test_tls_diagnostics)
      add_executable(sbproxy_test ../native/sbproxy/sbproxy.c
        ../native/sbproxy/hls_playlist.c ../native/sbproxy/hls_audio.c ../native/sbproxy/hls_relay.c
        ../native/sbproxy/hls_id3.c ../native/sbproxy/hls_metadata.c)
      target_compile_definitions(sbproxy_test PRIVATE _GNU_SOURCE SB_PROXY_TESTING)
      target_link_libraries(sbproxy_test PRIVATE CURL::libcurl Threads::Threads)
      add_test(NAME proxy_tls_integration COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tls/test_proxy_tls.py $<TARGET_FILE:sbproxy_test>)
      add_executable(test_hls images/test_hls.c ../native/sbproxy/hls_playlist.c ../native/sbproxy/hls_audio.c ../native/sbproxy/hls_id3.c)
      add_executable(test_hls_id3 images/test_hls_id3.c ../native/sbproxy/hls_id3.c)
      add_test(NAME hls_id3 COMMAND test_hls_id3)
      add_executable(test_hls_wait images/test_hls_wait.c)
      target_link_libraries(test_hls_wait PRIVATE Threads::Threads)
      add_test(NAME hls_metadata_wait COMMAND test_hls_wait)
      target_link_libraries(test_hls PRIVATE CURL::libcurl)
      add_test(NAME hls_native COMMAND test_hls)
      add_test(NAME hls_streaming COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/images/test_hls_http.py $<TARGET_FILE:sbproxy_test>)
      set_tests_properties(hls_streaming PROPERTIES ENVIRONMENT "SB_HLS_TEST_TIMEOUT=1")
      add_test(NAME artwork_proxy_streaming COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/images/test_artwork_proxy.py $<TARGET_FILE:sbproxy_test>)
      add_test(NAME image_proxy_guard COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/images/test_proxy_guard.py $<TARGET_FILE:sbproxy>)
    endif()
    add_test(NAME comet_connections COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/comet/test_connections.py $<TARGET_FILE:sbbase>)
    add_test(NAME comet_radio_shared COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/comet/test_radio_shared.py $<TARGET_FILE:sbbase>)
    add_test(NAME comet_standby COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/comet/test_standby.py $<TARGET_FILE:sbbase>)
    set_tests_properties(comet_standby PROPERTIES TIMEOUT 30)
    set_tests_properties(comet_radio_shared PROPERTIES TIMEOUT 30)
    add_test(NAME alarms_comet COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/alarms/test_comet.py $<TARGET_FILE:sbbase>)
    add_test(NAME firmware_comet COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/firmware/test_comet.py $<TARGET_FILE:sbbase>)
    add_test(NAME comet_menustatus COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/comet/test_menustatus.py $<TARGET_FILE:sbbase>)
    add_test(NAME comet_server_switch COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/comet/test_server_switch.py $<TARGET_FILE:sbbase>)
    add_test(NAME comet_catalog_targets COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/comet/test_catalog_targets.py $<TARGET_FILE:sbbase>)
    add_test(NAME nowplaying_poc COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/comet/test_nowplaying.py $<TARGET_FILE:sbbase>)
    add_test(NAME nowplaying_api COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/comet/test_nowplaying_api.py $<TARGET_FILE:sbbase>)
    add_test(NAME lpt_geometry COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/storage/test_lpt_geometry.py)
    add_test(NAME superblock_patch COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/storage/test_superblock_patch.py)
    add_test(NAME remote_http COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/remote/test_remote_http.py $<TARGET_FILE:sbwebserver> ${CMAKE_SOURCE_DIR}/native/sbwebserver/web)
    add_test(NAME radio_management_http COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/radio/test_radio_http.py $<TARGET_FILE:sbwebserver> ${CMAKE_SOURCE_DIR}/native/sbwebserver/web)
    add_test(NAME web_access_pairing COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/auth/test_web_pairing.py $<TARGET_FILE:sbwebserver> ${CMAKE_SOURCE_DIR}/native/sbwebserver/web)
    add_test(NAME web_remember_browser COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/auth/test_remember_browser.py $<TARGET_FILE:sbwebserver> ${CMAKE_SOURCE_DIR}/native/sbwebserver/web)
    set_tests_properties(web_remember_browser PROPERTIES TIMEOUT 60)
    add_test(NAME configuration_backup COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/backup/test_backup.py $<TARGET_FILE:sbwebserver> ${CMAKE_SOURCE_DIR}/native/sbwebserver/web $<TARGET_FILE:backup_rename_fault>)
    add_test(NAME system_log_viewer COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/logs/test_logs_http.py $<TARGET_FILE:sbwebserver> ${CMAKE_SOURCE_DIR}/native/sbwebserver/web)
    add_test(NAME process_monitor COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/monitor/test_monitor_http.py $<TARGET_FILE:sbwebserver> ${CMAKE_SOURCE_DIR}/native/sbwebserver/web)
    add_test(NAME cpu_accounting_diagnostic COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/monitor/test_cpu_diagnostic.py)
    set_tests_properties(applet_repository svg_image_http remote_http
      radio_management_http web_access_pairing system_log_viewer process_monitor
      PROPERTIES ENVIRONMENT "SB_CATALOG_SYNC_DISABLED=1")
  endif()
endif()

# WP0 pure contracts and typed command boundary tests; no local music runtime.
add_executable(test_music_transport music/test_transport.c)
target_compile_options(test_music_transport PRIVATE -UNDEBUG)
target_link_libraries(test_music_transport PRIVATE sbtransport)
add_test(NAME music_transport COMMAND test_music_transport)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
add_library(music_rendezvous_paths SHARED music/rendezvous_paths.c)
target_link_libraries(music_rendezvous_paths PRIVATE dl)
endif()
add_executable(test_music_resource_rendezvous music/test_resource_rendezvous.c ../native/common/music_resource_ipc.c)
target_compile_options(test_music_resource_rendezvous PRIVATE -UNDEBUG)
target_include_directories(test_music_resource_rendezvous PRIVATE ../native/common)
add_test(NAME music_resource_rendezvous COMMAND test_music_resource_rendezvous)
if(Python3_Interpreter_FOUND AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT CMAKE_CROSSCOMPILING)
  add_test(NAME music_transport_wire COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/music/test_transport_wire.py $<TARGET_FILE:test_music_transport>)
  add_test(NAME base_backpressure COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/comet/test_backpressure.py $<TARGET_FILE:sbbase> $<TARGET_FILE:base_socket_fault>)
  add_library(base_socket_fault SHARED comet/socket_fault.c)
  target_link_libraries(base_socket_fault PRIVATE ${CMAKE_DL_LIBS})
  add_test(NAME base_partial_writes COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/comet/test_backpressure.py $<TARGET_FILE:sbbase> $<TARGET_FILE:base_socket_fault> --partial)
endif()
add_executable(test_music_contracts music/test_contracts.c ../native/common/music_contracts.c ../native/sbbase/src/nowplaying.c)
target_include_directories(test_music_contracts PRIVATE ../native/sbbase/include)
add_test(NAME music_contracts COMMAND test_music_contracts)
add_executable(test_music_commands music/test_commands.c
  ../native/sbbase/src/lms_command.c ../native/sbbase/src/nowplaying.c)
target_include_directories(test_music_commands PRIVATE ../native/sbbase/include)
add_test(NAME music_commands COMMAND test_music_commands)
if(NOT TARGET test_hls_id3)
  add_executable(test_hls_id3 ../tests/images/test_hls_id3.c ../native/sbproxy/hls_id3.c)
  add_test(NAME hls_id3 COMMAND test_hls_id3)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT TARGET test_hls_wait)
  add_executable(test_hls_wait images/test_hls_wait.c)
  target_link_libraries(test_hls_wait PRIVATE Threads::Threads)
  add_test(NAME hls_metadata_wait COMMAND test_hls_wait)
endif()

if(Python3_Interpreter_FOUND AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT CMAKE_CROSSCOMPILING)
  add_test(NAME music_dispatch_http COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/music/test_dispatch_http.py $<TARGET_FILE:sbbase>)
  add_test(NAME music_wire_fixtures COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/music/test_wire_fixtures.py)
endif()

if(Python3_Interpreter_FOUND AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT CMAKE_CROSSCOMPILING)
  add_test(NAME music_fd_contract COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/music/test_fd_contract.py)
endif()

if(TARGET sbmusic AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(test_music_scanner music/test_scanner.c)
  target_compile_options(test_music_scanner PRIVATE -UNDEBUG)
  target_link_libraries(test_music_scanner PRIVATE sbmusic_scanner sbmusic_metadata)
  add_executable(music_metadata_fault music/metadata_fault.c)
  target_link_libraries(music_metadata_fault PRIVATE sbmusic_metadata)
  if(Python3_Interpreter_FOUND AND NOT CMAKE_CROSSCOMPILING)
    add_test(NAME music_scanner COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/music/test_scanner.py $<TARGET_FILE:test_music_scanner>
      $<TARGET_FILE:sbmusic_metadata_helper> $<TARGET_FILE:music_metadata_fault>)
  endif()
  add_executable(test_music_storage music/test_storage.c)
  target_compile_options(test_music_storage PRIVATE -UNDEBUG)
  target_link_libraries(test_music_storage PRIVATE sbmusic_storage)
  if(Python3_Interpreter_FOUND AND NOT CMAKE_CROSSCOMPILING)
    add_test(NAME music_storage_foundation COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/music/test_storage.py $<TARGET_FILE:test_music_storage>)
    find_program(SB_STORAGE_LUA lua)
    if(SB_STORAGE_LUA)
      add_test(NAME music_storage_lua COMMAND ${SB_STORAGE_LUA}
        ${CMAKE_CURRENT_SOURCE_DIR}/music/test_storage_lua.lua ${CMAKE_SOURCE_DIR})
    endif()
  endif()
  add_executable(test_music_library music/test_library.c)
  target_compile_options(test_music_library PRIVATE -UNDEBUG)
  target_link_libraries(test_music_library PRIVATE sbmusic)
  add_test(NAME music_integrity_worker COMMAND test_music_library integrity ${CMAKE_CURRENT_BINARY_DIR}/integrity-test.db)
  if(Python3_Interpreter_FOUND AND NOT CMAKE_CROSSCOMPILING)
    add_test(NAME music_library_storage COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/music/test_library_storage.py $<TARGET_FILE:test_music_library>)
    add_test(NAME music_library_large COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/music/test_library_large.py $<TARGET_FILE:test_music_library>)
  endif()
endif()

if(TARGET sbmusic_queue)
 add_executable(test_music_queue music/test_queue.c ../native/common/music_contracts.c)
 target_compile_options(test_music_queue PRIVATE -UNDEBUG -Wall -Wextra -Wpedantic)
 target_link_libraries(test_music_queue PRIVATE sbmusic_queue)
 add_executable(test_music_http_policy music/test_music_http.c ../native/common/music_http.c)
 target_include_directories(test_music_http_policy PRIVATE ../native/common)
 target_compile_options(test_music_http_policy PRIVATE -UNDEBUG -Wall -Wextra -Wpedantic)
 add_executable(test_music_stream music/test_stream.c ../native/sbwebserver/src/music_stream.c ../native/common/music_resource_ipc.c)
 add_executable(test_music_stream_pump music/test_stream_pump.c ../native/sbwebserver/src/music_stream_http.c ../native/sbwebserver/src/music_stream.c ../native/sbwebserver/third_party/mongoose.c ../native/common/music_resource_ipc.c ../native/common/music_http.c)
 target_include_directories(test_music_stream_pump PRIVATE ../native/sbwebserver/src ../native/sbwebserver/third_party ../native/common)
 target_compile_definitions(test_music_stream_pump PRIVATE _GNU_SOURCE _POSIX_C_SOURCE=200809L _FILE_OFFSET_BITS=64 MG_ENABLE_EPOLL=0 MG_ENABLE_POLL=1 MG_ENABLE_DIRLIST=0 MG_ENABLE_SSI=0)
 target_compile_options(test_music_stream_pump PRIVATE -UNDEBUG -Wall -Wextra -Wpedantic)
 target_link_libraries(test_music_stream_pump PRIVATE Threads::Threads)
 target_include_directories(test_music_stream PRIVATE ../native/sbwebserver/src)
 target_compile_definitions(test_music_stream PRIVATE _FILE_OFFSET_BITS=64)
 target_compile_options(test_music_stream PRIVATE -UNDEBUG -Wall -Wextra -Wpedantic)
 target_link_libraries(test_music_stream PRIVATE Threads::Threads)
 add_executable(test_music_playback music/test_playback.c ../native/sbbase/src/nowplaying.c
   ../native/sbbase/src/lms_command.c ../native/sbwebserver/src/music_stream.c)
 target_include_directories(test_music_playback PRIVATE ../native/sbwebserver/src ../native/common)
 target_compile_options(test_music_playback PRIVATE -UNDEBUG -Wall -Wextra -Wpedantic)
 target_link_libraries(test_music_playback PRIVATE sbmusic_playback sbmusic_browse sbmusic_scanner)
 add_executable(test_music_load music/test_load.c ../native/sbbase/src/nowplaying.c ../native/sbbase/src/lms_command.c)
 target_compile_options(test_music_load PRIVATE -UNDEBUG -Wall -Wextra -Wpedantic)
 target_link_libraries(test_music_load PRIVATE sbmusic_load)
 if(NOT CMAKE_CROSSCOMPILING)
  add_test(NAME music_queue COMMAND test_music_queue)
  add_test(NAME music_http_policy COMMAND test_music_http_policy)
  add_test(NAME music_stream COMMAND test_music_stream)
  add_test(NAME music_stream_pump COMMAND test_music_stream_pump)
  add_test(NAME music_playback COMMAND test_music_playback $<TARGET_FILE:sbmusic_metadata_helper> ${CMAKE_CURRENT_SOURCE_DIR}/music/fixtures/media)
  add_test(NAME music_playback_single_mp3 COMMAND test_music_playback $<TARGET_FILE:sbmusic_metadata_helper> ${CMAKE_CURRENT_SOURCE_DIR}/music/fixtures/media single-mp3)
  add_test(NAME music_load COMMAND test_music_load)
  if(LUA_EXECUTABLE)
   add_test(NAME music_ownership_lua COMMAND ${LUA_EXECUTABLE}
     ${CMAKE_CURRENT_SOURCE_DIR}/music/test_ownership_lua.lua ${CMAKE_SOURCE_DIR})
  endif()
  if(Python3_Interpreter_FOUND)
   add_test(NAME music_stream_http COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/music/test_stream_http.py $<TARGET_FILE:sbwebserver_music_test>)
   add_test(NAME music_playback_http COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/music/test_playback_http.py
     $<TARGET_FILE:sbbase_music_test> $<TARGET_FILE:sbwebserver_music_test> $<TARGET_FILE:test_music_scanner> $<TARGET_FILE:sbmusic_metadata_helper>)
   add_test(NAME music_playback_ownership COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/music/test_playback_http.py
     $<TARGET_FILE:sbbase_music_test> $<TARGET_FILE:sbwebserver_music_test> $<TARGET_FILE:test_music_scanner> $<TARGET_FILE:sbmusic_metadata_helper> owners)
  endif()
 endif()
endif()

if(TARGET sbmusic_browse)
 add_executable(test_music_browse music/test_browse.c ../native/sbbase/src/nowplaying.c ../native/sbbase/src/lms_command.c)
 target_compile_options(test_music_browse PRIVATE -UNDEBUG)
 target_link_libraries(test_music_browse PRIVATE sbmusic_browse sbmusic_scanner)
endif()

if(TARGET test_music_browse AND Python3_Interpreter_FOUND AND NOT CMAKE_CROSSCOMPILING)
 add_test(NAME music_browse_native COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/music/test_browse.py $<TARGET_FILE:test_music_browse> $<TARGET_FILE:sbmusic_metadata_helper>)
 add_test(NAME music_browse_http COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/music/test_browse_http.py $<TARGET_FILE:sbbase_browse_test> $<TARGET_FILE:test_music_browse> long)
 add_test(NAME music_browse_partial COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/music/test_browse_http.py $<TARGET_FILE:sbbase_browse_test> $<TARGET_FILE:test_music_browse> $<TARGET_FILE:base_socket_fault> long)
 add_test(NAME music_browse_empty COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/music/test_browse_empty.py $<TARGET_FILE:sbbase_browse_test> $<TARGET_FILE:test_music_browse>)
 add_test(NAME music_browse_faults COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/music/test_browse_faults.py $<TARGET_FILE:sbbase_browse_test> $<TARGET_FILE:test_music_browse>)
 add_test(NAME music_browse_disabled COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/music/test_browse_disabled.py $<TARGET_FILE:sbbase> $<TARGET_FILE:sbbase_browse_test>)
 add_test(NAME music_slim_keepalive COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/music/test_slim_keepalive.py $<TARGET_FILE:sbbase_browse_test> $<TARGET_FILE:test_music_browse>)
 add_test(NAME music_browse_overflow COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/music/test_browse_overflow.py $<TARGET_FILE:sbbase_browse_test> $<TARGET_FILE:test_music_browse>)
 add_test(NAME music_backlog_drain COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/music/test_backlog_drain.py $<TARGET_FILE:sbbase_browse_test> $<TARGET_FILE:test_music_browse>)
endif()

if(UNIX AND TARGET sbtransport)
 add_executable(test_ownership_driver music/ownership_driver.c ../native/sbbase/src/ownership_bridge.c ../native/common/music_resource_ipc.c)
 target_include_directories(test_ownership_driver PRIVATE ../native/sbbase/include)
 target_compile_options(test_ownership_driver PRIVATE -UNDEBUG -Wall -Wextra -Wpedantic)
 target_link_libraries(test_ownership_driver PRIVATE sbtransport)
 if(Python3_Interpreter_FOUND AND NOT CMAKE_CROSSCOMPILING)
  add_test(NAME music_ownership_bridge COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/music/test_ownership_bridge.py $<TARGET_FILE:test_ownership_driver>)
  add_test(NAME music_idle_admission COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/music/test_idle_admission.py $<TARGET_FILE:test_ownership_driver>)
 endif()
endif()

if(TARGET test_ownership_driver AND Python3_Interpreter_FOUND AND NOT CMAKE_CROSSCOMPILING)
 add_test(NAME music_ownership_http COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/music/test_ownership_http.py $<TARGET_FILE:sbbase>)
endif()

if(LUA_EXECUTABLE)
 add_test(NAME tinysc_ui_heartbeat COMMAND ${LUA_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/music/test_lifecycle_heartbeat.lua ${CMAKE_SOURCE_DIR})
 add_test(NAME music_ownership_firmware_edges COMMAND ${LUA_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/music/test_ownership_firmware_edges.lua ${CMAKE_SOURCE_DIR})
endif()

option(SB_TEST_FIRMWARE_ADAPTERS "Run sibling app adapter tests with development-only lupa" OFF)
if(TARGET sbmusic-session AND Python3_Interpreter_FOUND)
 add_test(NAME music_store_launcher COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/music/test_store_launcher.py $<TARGET_FILE:sbmusic-session>)
 set_tests_properties(music_store_launcher PROPERTIES TIMEOUT 30)
endif()
set(SB_RADIO_SOURCE "${CMAKE_SOURCE_DIR}/../../squeezebox" CACHE PATH "Radio source for ownership fixtures")
set(SB_SPOTIFY_SOURCE "${CMAKE_SOURCE_DIR}/../../SBSpotifyConnect" CACHE PATH "Spotify source for ownership fixtures")
set(SB_QOBUZ_SOURCE "${CMAKE_SOURCE_DIR}/../../QoBuz" CACHE PATH "Qobuz source for ownership fixtures")
if(Python3_Interpreter_FOUND AND EXISTS "${SB_RADIO_SOURCE}/applet/StandaloneRadio/StreamPlayer.lua" AND EXISTS "${SB_SPOTIFY_SOURCE}/applet/SpotifyService.lua")
  add_test(NAME music_idle_fence COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/music/test_music_idle_fence.py
    ${CMAKE_SOURCE_DIR} ${SB_RADIO_SOURCE} ${SB_SPOTIFY_SOURCE})
  set_tests_properties(music_idle_fence PROPERTIES SKIP_RETURN_CODE 77)
endif()
if(SB_TEST_FIRMWARE_ADAPTERS AND TARGET test_ownership_driver AND Python3_Interpreter_FOUND AND NOT CMAKE_CROSSCOMPILING)
 add_test(NAME music_firmware_adapters COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/music/test_firmware_ownership.py
  $<TARGET_FILE:test_ownership_driver> ${CMAKE_SOURCE_DIR} ${SB_RADIO_SOURCE} ${SB_SPOTIFY_SOURCE} ${SB_QOBUZ_SOURCE})
endif()

if(TARGET sbbase_music AND TARGET sbwebserver_music AND TARGET sbmusic-session AND Python3_Interpreter_FOUND AND NOT CMAKE_CROSSCOMPILING)
 add_test(NAME music_production_bootstrap COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/music/test_production_bootstrap.py
  $<TARGET_FILE:sbbase_music> $<TARGET_FILE:sbwebserver_music> $<TARGET_FILE:sbmusic-session>)
 add_test(NAME music_production_resource_ipc COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/music/test_stream_http.py
  $<TARGET_FILE:sbwebserver_music> --production)
 if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
 add_test(NAME music_rendezvous_pairing COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/music/test_rendezvous_pairing.py
  $<TARGET_FILE:sbbase_music_host_test> $<TARGET_FILE:sbwebserver_music> $<TARGET_FILE:test_music_scanner> $<TARGET_FILE:sbmusic_metadata_helper> $<TARGET_FILE:music_rendezvous_paths>)
 add_test(NAME music_store_pairing COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/music/test_store_pairing.py
  $<TARGET_FILE:sbbase_music_host_test> $<TARGET_FILE:sbwebserver_music> $<TARGET_FILE:test_music_scanner> $<TARGET_FILE:sbmusic_metadata_helper> $<TARGET_FILE:music_rendezvous_paths>)
 endif()
endif()

if(UNIX AND TARGET sbbase)
 add_executable(sb_maintenance_helper ../native/sbmaintenance/helper.c)
 target_compile_options(sb_maintenance_helper PRIVATE -Wall -Wextra -Wpedantic -Werror)
 add_executable(sb_tinysc_control ../native/sblifecycle/tinysc.c ../native/sblifecycle/control.c)
 target_link_libraries(sb_tinysc_control PRIVATE Threads::Threads)
 target_compile_options(sb_tinysc_control PRIVATE -Wall -Wextra -Wpedantic -Werror)
 if(Python3_Interpreter_FOUND AND NOT CMAKE_CROSSCOMPILING)
  add_executable(maintenance_helper_fixture ../native/sbmaintenance/helper.c)
  target_compile_definitions(maintenance_helper_fixture PRIVATE SB_MAINTENANCE_TEST=1)
  target_compile_options(maintenance_helper_fixture PRIVATE -Wall -Wextra -Wpedantic -Werror)
  add_executable(maintenance_jive_fixture music/maintenance_jive_fixture.c)
  add_executable(restart_probe_fixture music/restart_probe_fixture.c)
  target_compile_options(restart_probe_fixture PRIVATE -Wall -Wextra -Wpedantic -Werror)
  add_test(NAME maintenance_primitives COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/music/test_maintenance_helper.py
    $<TARGET_FILE:maintenance_helper_fixture> $<TARGET_FILE:sb_maintenance_helper> $<TARGET_FILE:maintenance_jive_fixture> -v)
  add_test(NAME maintenance_probe COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/music/test_restart_probe.py
    $<TARGET_FILE:maintenance_helper_fixture> $<TARGET_FILE:restart_probe_fixture> -v)
  add_test(NAME maintenance_stack COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/music/test_stack_topology.py
    $<TARGET_FILE:maintenance_helper_fixture> $<TARGET_FILE:restart_probe_fixture> -v)
  add_test(NAME maintenance_executable_identity COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/music/test_executable_identity.py
    $<TARGET_FILE:maintenance_helper_fixture> $<TARGET_FILE:restart_probe_fixture> -v)
  add_test(NAME maintenance_music_generation COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/music/test_music_generation.py -v)
  add_test(NAME maintenance_coordinator COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/music/test_restart_coordinator.py
    $<TARGET_FILE:maintenance_helper_fixture> $<TARGET_FILE:restart_probe_fixture> -v)
  add_test(NAME maintenance_internal_files COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/music/test_internal_install.py
    $<TARGET_FILE:maintenance_helper_fixture> -v)
  add_test(NAME maintenance_deployment_rollback COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/music/test_lifecycle_deployment.py
    $<TARGET_FILE:maintenance_helper_fixture> -v)
  add_test(NAME maintenance_music_deployment_rollback COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/music/test_lifecycle_deployment.py
    $<TARGET_FILE:maintenance_helper_fixture> --music -v)
  add_test(NAME maintenance_music_recovery COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/music/test_music_qualification.py
    $<TARGET_FILE:maintenance_helper_fixture> -v)
  add_test(NAME applet_only_deployment COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/music/test_applet_only_deployment.py -v)
  set_tests_properties(applet_only_deployment PROPERTIES TIMEOUT 90)
  set_tests_properties(maintenance_primitives maintenance_probe maintenance_coordinator
    PROPERTIES RESOURCE_LOCK standalone_music_ipc TIMEOUT 90)
  add_executable(tinysc_control_fixture ../native/sblifecycle/tinysc.c ../native/sblifecycle/control.c)
  add_executable(tinysc_lifecycle_fixture ../native/sblifecycle/tinysc.c music/lifecycle_fixture.c)
  foreach(target tinysc_control_fixture tinysc_lifecycle_fixture)
   target_compile_definitions(${target} PRIVATE SB_TINYSC_TEST=1)
   target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
   target_link_libraries(${target} PRIVATE Threads::Threads)
  endforeach()
  add_test(NAME tinysc_lifecycle COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/music/test_tinysc_lifecycle.py
    $<TARGET_FILE:tinysc_control_fixture> $<TARGET_FILE:tinysc_lifecycle_fixture> -v)
  set_tests_properties(tinysc_lifecycle PROPERTIES TIMEOUT 30)
 endif()
endif()

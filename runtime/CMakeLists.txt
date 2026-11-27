# peanuts-run: the Peanuts Native host (igawa6/peanuts-native, the Android build's libnxhost) for Linux arm64 with glibc, as one program that plays a built game.
# It is the generic host (one binary for every title: the title's profile is read at run time, DM_PROFILE_PATH), built from the same sources as
# android/host/CMakeLists.txt's nxhost_add_generic, with the Android-only pieces replaced by shim/ (Android NDK headers implemented on SDL3, FFmpeg and glibc).
#   cmake -S runtime -B <build> -DPN_SRC=<patched peanuts-native tree> -DCMAKE_TOOLCHAIN_FILE=<aarch64 toolchain> -DPNL_PREFIX=<SDL3 and FFmpeg prefix>
cmake_minimum_required(VERSION 3.22)
project(peanuts_run C CXX ASM)
if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
  message(FATAL_ERROR "the host runs the game's ARM64 code directly: build for aarch64")
endif()
set(PN_SRC "" CACHE PATH "the patched peanuts-native tree (scripts/patch_upstream.py)")
set(PNL_PREFIX "/opt/a64" CACHE PATH "where SDL3, FFmpeg (libavcodec, libavutil) and zlib for aarch64 are installed")
if(NOT EXISTS "${PN_SRC}/dm/host/main.c")
  message(FATAL_ERROR "PN_SRC=${PN_SRC} is not a peanuts-native tree")
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(ROOT ${PN_SRC})
set(HOST ${ROOT}/dm/host)
set(GFX ${ROOT}/dm/gfx)
set(TP ${ROOT}/dm/third_party)
set(GEN ${CMAKE_CURRENT_BINARY_DIR}/gen)
set(SHIM ${CMAKE_CURRENT_SOURCE_DIR}/shim/include)
set(PNL ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)

# ---- The host's assembly, Mach-O spelling → ELF (as the Android build) ----
set(ASM_OUT)
foreach(s ${HOST}/entry.S ${GFX}/nvn/nvn_entry.S ${GFX}/nvn/nvn_thunks.S)
  get_filename_component(n ${s} NAME_WE)
  set(o ${GEN}/asm/${n}.S)
  add_custom_command(OUTPUT ${o} COMMAND ${Python3_EXECUTABLE} ${ROOT}/android/host/macho_asm_to_elf.py ${s} ${o} DEPENDS ${s} ${ROOT}/android/host/macho_asm_to_elf.py)
  list(APPEND ASM_OUT ${o})
endforeach()

# ---- The host's C sources: android/host/CMakeLists.txt's list, with front_android.c and the JNI bindings replaced ----
file(GLOB HOST_C CONFIGURE_DEPENDS ${HOST}/*.c)
list(REMOVE_ITEM HOST_C ${HOST}/hle_movie_avc.c)   # VideoToolbox: movie_avc_android.c (over pnl_mediacodec.c) stands in
list(APPEND HOST_C ${HOST}/plat/plat_android.c ${HOST}/plat/movie_avc_android.c ${PNL}/front_linux.c)
file(GLOB HOST_ANDROID_C CONFIGURE_DEPENDS ${HOST}/android/*.c)
list(REMOVE_ITEM HOST_ANDROID_C ${HOST}/android/jni_android.c)   # no Java VM: peanuts-run calls input_android.h's functions directly
list(APPEND HOST_C ${HOST_ANDROID_C})
foreach(f peanuts peanuts_nup peanuts_services peanuts_graph peanuts_frametime peanuts_audiosync peanuts_audiovoice peanuts_gpufloor)
  list(APPEND HOST_C ${ROOT}/dm/peanuts/${f}.c)
endforeach()
set(NSP_C ${ROOT}/dm/peanuts/peanuts_nsp.c ${ROOT}/dm/peanuts/import/nx_nca.c ${ROOT}/dm/peanuts/import/nx_crypto.c ${ROOT}/dm/peanuts/import/nx_lz4.c)
set_source_files_properties(${NSP_C} PROPERTIES COMPILE_OPTIONS -O2)
list(APPEND HOST_C ${NSP_C})
set(NX_COOP_VENDOR 0)
if(EXISTS "${TP}/pokopia-coop/coop.h")
  foreach(part ldn vnet mpnet plat auth disc)
    list(APPEND HOST_C "${TP}/pokopia-coop/coop_${part}.c")
  endforeach()
  set(NX_COOP_VENDOR 1)
endif()
set_source_files_properties(${HOST}/main.c PROPERTIES COMPILE_DEFINITIONS "main=dm_host_main")   # the program's main is peanuts-run's

file(GLOB GFX_C CONFIGURE_DEPENDS ${GFX}/*.c ${GFX}/nvn/*.c ${GFX}/tex/*.c ${GFX}/vk_android/*.c ${GFX}/vk_android/lsfg/*.c)
file(GLOB VKA_C ${GFX}/vk_android/*.c)
set_source_files_properties(${VKA_C} PROPERTIES COMPILE_OPTIONS -O2)

# ---- Third party, as the Android build ----
file(GLOB_RECURSE VPX_C CONFIGURE_DEPENDS ${TP}/libvpx/*.c)
add_library(vpx STATIC ${VPX_C})
target_include_directories(vpx PRIVATE ${TP}/libvpx ${TP}/libvpx/config)
target_compile_options(vpx PRIVATE -O2 -w -fPIC)
add_library(zstddec STATIC ${TP}/zstd/zstddeclib.c)
target_compile_options(zstddec PRIVATE -O2 -w -fvisibility=hidden -fPIC)
file(GLOB DAV1D_C CONFIGURE_DEPENDS ${TP}/dav1d/src/*.c ${TP}/dav1d/config/tmpl/*.c)
list(FILTER DAV1D_C EXCLUDE REGEX "_tmpl\\.c$")
list(APPEND DAV1D_C ${TP}/dav1d/src/arm/cpu.c)
file(GLOB DAV1D_S CONFIGURE_DEPENDS ${TP}/dav1d/src/arm/64/*.S)
list(FILTER DAV1D_S EXCLUDE REGEX "(_tmpl|/util)\\.S$")
add_library(dav1d STATIC ${DAV1D_C} ${DAV1D_S})
target_include_directories(dav1d PRIVATE ${ROOT}/android/host/dav1d_config ${TP}/dav1d ${TP}/dav1d/include ${TP}/dav1d/config)
target_compile_options(dav1d PRIVATE -O2 -w -fvisibility=hidden -fPIC)
target_compile_definitions(dav1d PRIVATE _GNU_SOURCE)

add_subdirectory(${ROOT}/dm/peanuts/rt ${CMAKE_CURRENT_BINARY_DIR}/peanuts_rt)   # the companion runtime (C++; builds for Linux as it is)

# ---- The generic host's profile glue (android/host's nxhost_add_generic) ----
set(PROFILE_DIR ${ROOT}/dm/peanuts/profile)
set(GEN_INC ${GEN}/generic)
configure_file(${PROFILE_DIR}/game_profile_generic.h ${GEN_INC}/game_profile.h COPYONLY)
configure_file(${PROFILE_DIR}/vk_prewarm_seed_generic.h ${GEN_INC}/vk_prewarm_seed.h COPYONLY)
set(SCHEMA_H ${GEN}/schema_stamp.h)
add_custom_command(OUTPUT ${SCHEMA_H} COMMAND ${Python3_EXECUTABLE} ${PROFILE_DIR}/gen_runtime_profile.py schema-stamp ${SCHEMA_H}
  DEPENDS ${PROFILE_DIR}/gen_runtime_profile.py ${ROOT}/dm/games/game.py COMMENT "generic host schema stamp")

# ---- The host: compiled as the Android build (-D__ANDROID__: the Bionic/Vulkan paths), against the shim's NDK headers ----
add_library(nxhost STATIC ${HOST_C} ${GFX_C} ${ASM_OUT} ${PROFILE_DIR}/profile_rt.c ${SCHEMA_H})
target_include_directories(nxhost BEFORE PRIVATE ${SHIM})
target_include_directories(nxhost PRIVATE ${GEN_INC} ${GEN} ${PROFILE_DIR} ${GFX}/nvn/include ${TP}/vulkan-headers/include ${TP}/libvpx ${TP}/libvpx/config
  ${TP}/dav1d/include ${TP}/stb_vorbis ${HOST} ${TP}/pokopia-coop ${PNL_PREFIX}/include)
target_compile_definitions(nxhost PRIVATE __ANDROID__=1 PEANUTS_LINUX=1 _GNU_SOURCE DM_NUP_ZSTD DM_NSP_DIRECT NX_COOP_VENDOR=${NX_COOP_VENDOR} NX_LSFG)
target_compile_options(nxhost PRIVATE -O1 -g -Wall -Wno-unused-function -Wno-format -fvisibility=hidden -fPIC -fno-strict-aliasing
  -Wno-unused-variable -Wno-unused-but-set-variable -Wno-stringop-truncation -Wno-format-truncation -Wno-misleading-indentation -Wno-maybe-uninitialized
  -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast -Wno-address-of-packed-member -Wno-array-bounds -Wno-stringop-overflow -Wno-dangling-pointer
  -Wno-unused-result -Wno-parentheses -Wno-switch -Wno-enum-int-mismatch -Wno-restrict -Wno-use-after-free -Wno-nonnull
  -Wno-unknown-warning-option "$<$<COMPILE_LANGUAGE:C>:SHELL:-include ${SHIM}/pnl_compat.h>")   # Bionic's declarations glibc lacks (C only: not the assembly)
target_link_libraries(nxhost PRIVATE peanuts_rt)
add_dependencies(nxhost peanuts_rt)

# ---- The Linux glue: shims on SDL3 / FFmpeg / glibc (no __ANDROID__ here: SDL's headers would take it for Android) ----
add_library(pnl_shim STATIC ${PNL}/pnl_window.c ${PNL}/pnl_aaudio.c ${PNL}/pnl_mediacodec.c ${PNL}/pnl_misc.c ${PNL}/pnl_companion.c)
target_include_directories(pnl_shim PRIVATE ${SHIM} ${PNL} ${PNL_PREFIX}/include ${TP}/vulkan-headers/include)
target_compile_options(pnl_shim PRIVATE -O2 -g -Wall -fPIC)
target_compile_definitions(pnl_shim PRIVATE _GNU_SOURCE)

# ---- peanuts-run: pnl_tls.c FIRST (its thread-local word must lead the executable's TLS segment: pnl_tls.c) ----
# The host goes in whole (--whole-archive: its HLE tables are sections every object adds to, reached by no symbol), after pnl_tls.o and pnl_run.o
add_executable(peanuts-run ${PNL}/pnl_tls.c ${PNL}/pnl_run.c ${PNL}/pnl_menu.c)
set_source_files_properties(${PNL}/pnl_menu.c PROPERTIES COMPILE_OPTIONS "-Wno-unused-function")   # (stb_truetype: the parts the menu does not use)
target_include_directories(peanuts-run PRIVATE ${SHIM} ${PNL} ${HOST}/android ${HOST} ${PNL_PREFIX}/include)
target_compile_options(peanuts-run PRIVATE -O2 -g -Wall)
set_target_properties(peanuts-run PROPERTIES LINKER_LANGUAGE CXX)
target_link_directories(peanuts-run PRIVATE ${PNL_PREFIX}/lib)
target_link_libraries(peanuts-run PRIVATE -Wl,--whole-archive nxhost -Wl,--no-whole-archive pnl_shim peanuts_rt vpx dav1d zstddec ${PNL_PREFIX}/lib/libavcodec.a ${PNL_PREFIX}/lib/libavutil.a SDL3
  ${PNL_PREFIX}/lib/libz.a m dl pthread)
# Every exported entry point of the host stays (the generic host's weak symbols and tables are reached by name); the program's own libraries next to it
target_link_options(peanuts-run PRIVATE -Wl,--no-undefined -Wl,-z,noexecstack -Wl,--build-id -Wl,-rpath,$ORIGIN/../lib -rdynamic)
# The slot word must be the TLS segment's first word, and the segment at most 16-aligned (pnl_tls.c): checked on every link
add_custom_command(TARGET peanuts-run POST_BUILD COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_tls.py $<TARGET_FILE:peanuts-run> VERBATIM)

# ---- peanuts-bottom: the companion page's window on the bottom screen (ArmadaOS Game Mode: through armada-run-bottom) ----
add_executable(peanuts-bottom ${PNL}/peanuts_bottom.c)
target_include_directories(peanuts-bottom PRIVATE ${PNL} ${PNL_PREFIX}/include)
target_compile_options(peanuts-bottom PRIVATE -O2 -g -Wall)
target_compile_definitions(peanuts-bottom PRIVATE _GNU_SOURCE)
target_link_directories(peanuts-bottom PRIVATE ${PNL_PREFIX}/lib)
target_link_libraries(peanuts-bottom PRIVATE SDL3 dl)
target_link_options(peanuts-bottom PRIVATE -Wl,-z,noexecstack -Wl,--build-id -Wl,-rpath,$ORIGIN/../lib)

cmake_minimum_required(VERSION 3.18)

# Mesmo padrão de projeto do Switchfin (referência arquitetural — ver plano),
# simplificado pra só Desktop (iteração rápida, sem precisar do Switch toda
# hora) e Switch (alvo real). Sem PS4/PSVita/NSP forwarder, que o Switchfin
# suporta mas a gente não precisa.
set(BOREALIS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/library/borealis)
set(BOREALIS_LIBRARY ${BOREALIS_DIR}/library)

# Define PLATFORM_DESKTOP / PLATFORM_SWITCH etc.
# exemplo: cmake -B build -DPLATFORM_DESKTOP=ON
include(${BOREALIS_LIBRARY}/cmake/commonOption.cmake)

option(USE_SYSTEM_CURL "use system installed libcurl" ON)

option(MPV_SW_RENDER "Using CPU to draw videos" OFF)
if (MPV_SW_RENDER)
    list(APPEND APP_PLATFORM_OPTION -DMPV_SW_RENDER)
endif ()

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
elseif (CMAKE_BUILD_TYPE STREQUAL Debug)
    list(APPEND APP_PLATFORM_OPTION -D_DEBUG -D_GLIBCXX_ASSERTIONS)
endif ()

# Ativa o redirecionamento de stdout/stderr pra rede (nxlinkStdio(), em
# switch_wrapper.c) — só depende deste macro `DEBUG` puro (diferente do
# `_DEBUG` acima), então deixa sempre ligado pra poder investigar problemas
# via `nxlink` sem precisar reconfigurar o build inteiro toda hora.
if (PLATFORM_SWITCH)
    list(APPEND APP_PLATFORM_OPTION -DDEBUG)
endif ()

# toolchain (precisa vir antes do project())
include(${BOREALIS_LIBRARY}/cmake/toolchain.cmake)

project(IptvPlayerSwitch)
set(VERSION_MAJOR "1")
set(VERSION_MINOR "0")
set(VERSION_ALTER "2")
set(VERSION_BUILD "0")
set(PROJECT_AUTHOR "gomprime")
set(PACKAGE_NAME "com.iptvplayer.switch")

# Mesmo logo dos outros clientes do projeto (o hbmenu exige JPEG 256x256).
set(PROJECT_ICON ${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/icon.jpg)
set(PROJECT_RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/resources)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

# fontes e includes do projeto (não app/src, que era a convenção do Switchfin)
file(GLOB_RECURSE MAIN_SRC "${PROJECT_SOURCE_DIR}/source/*.cpp")
if (PLATFORM_SWITCH)
    # inicialização de serviços do libnx, exigida pela borealis
    list(APPEND MAIN_SRC ${BOREALIS_LIBRARY}/lib/platforms/switch/switch_wrapper.c)
elseif (WIN32)
    configure_file("${BOREALIS_DIR}/demo/resource.rc.in" "${CMAKE_BINARY_DIR}/resource.rc")
    list(APPEND MAIN_SRC ${CMAKE_BINARY_DIR}/resource.rc)
endif ()

# builda a borealis e o lunasvg
add_subdirectory(library)

if (USE_SYSTEM_CURL)
    find_package(CURL REQUIRED)
endif ()
list(APPEND APP_PLATFORM_LIB CURL::libcurl)

find_package(Threads REQUIRED)
list(APPEND APP_PLATFORM_LIB ${CMAKE_THREAD_LIBS_INIT})

if (PLATFORM_DESKTOP)
    find_package(MPV REQUIRED)
    list(APPEND APP_PLATFORM_INCLUDE ${MPV_INCLUDE_DIR})
    list(APPEND APP_PLATFORM_LIB ${MPV_LIBRARY})
    if (NOT WIN32)
        find_library(AVFORMAT_LIB NAMES avformat REQUIRED)
        list(APPEND APP_PLATFORM_LIB ${AVFORMAT_LIB})
    endif ()
    if (MINGW)
        set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++ -static")
    endif ()
else ()
    # Cross compile (Switch) — usa pkg-config, igual o Switchfin. Se as libs
    # não forem encontradas, dá pra apontar via variável de ambiente:
    # PKG_CONFIG_LIBDIR=$DEVKITPRO/portlibs/switch/lib/pkgconfig
    find_package(PkgConfig REQUIRED)
    pkg_search_module(MPV REQUIRED mpv)
    list(APPEND APP_PLATFORM_INCLUDE ${MPV_INCLUDE_DIRS})
    list(APPEND APP_PLATFORM_LIB ${MPV_LIBRARIES})
    link_directories(${MPV_LIBRARY_DIRS})
endif ()

# JSON — nlohmann::json vendorizada (header-only) em vez de jsoncpp: não
# existe portlib "switch-jsoncpp" pro alvo Switch (só a versão MSYS2/desktop
# do pacote "jsoncpp", confirmado ao configurar) — uma lib só de header não
# tem esse problema, não precisa de link nenhum.
list(APPEND APP_PLATFORM_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/source/third_party)

program_target(${PROJECT_NAME} "${MAIN_SRC}")
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17)
target_include_directories(${PROJECT_NAME} PRIVATE source ${APP_PLATFORM_INCLUDE})
target_compile_options(${PROJECT_NAME} PRIVATE -ffunction-sections -fdata-sections -Wunused-variable ${APP_PLATFORM_OPTION})
target_link_libraries(${PROJECT_NAME} PRIVATE borealis lunasvg ${APP_PLATFORM_LIB})
target_compile_definitions(${PROJECT_NAME} PRIVATE
    APP_VERSION=${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_ALTER}
    APP_BUILD=${VERSION_BUILD}
    BUILD_PACKAGE_NAME=${PROJECT_NAME}
)

if (PLATFORM_DESKTOP)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory ${PROJECT_RESOURCES} ${PROJECT_BINARY_DIR}/resources
    )
elseif (PLATFORM_SWITCH)
    add_custom_target(${PROJECT_NAME}.nro DEPENDS ${PROJECT_NAME}
        COMMAND ${NX_NACPTOOL_EXE} --create "IPTV Player" "${PROJECT_AUTHOR}" "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_ALTER}" ${PROJECT_NAME}.nacp
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_RESOURCES} ${CMAKE_BINARY_DIR}/resources
        COMMAND ${NX_ELF2NRO_EXE} ${PROJECT_NAME}.elf ${PROJECT_NAME}.nro --icon=${PROJECT_ICON} --nacp=${PROJECT_NAME}.nacp --romfsdir=${CMAKE_BINARY_DIR}/resources
    )
endif ()

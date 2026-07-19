cmake_minimum_required(VERSION 3.15)

# lunasvg (ícones/imagens SVG) — mesmo padrão do Switchfin.
set(BUILD_SHARED_LIBS OFF)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
add_subdirectory(lunasvg EXCLUDE_FROM_ALL)

# borealis (UI)
add_subdirectory(borealis/library)

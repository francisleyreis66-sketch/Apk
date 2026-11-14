cmake_minimum_required(VERSION 3.22.1)
project(retrogamesnes CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- shaders: GLSL -> SPIR-V (glslc vem junto com o NDK) ----
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
  set(GLSLC_HOST windows-x86_64)
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
  set(GLSLC_HOST darwin-x86_64)
else()
  set(GLSLC_HOST linux-x86_64)
endif()
find_program(GLSLC glslc
  HINTS "${ANDROID_NDK}/shader-tools/${GLSLC_HOST}" "${CMAKE_ANDROID_NDK}/shader-tools/${GLSLC_HOST}"
  NO_DEFAULT_PATH)
if(NOT GLSLC)
  find_program(GLSLC glslc)
endif()
if(NOT GLSLC)
  message(FATAL_ERROR "glslc não encontrado (ele vem no NDK, pasta shader-tools)")
endif()

set(SHADER_OUT ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_OUT})
set(SHADER_INCS "")
foreach(SH present.vert present.frag)
  set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SH})
  set(OUT ${SHADER_OUT}/${SH}.inc)
  add_custom_command(
    OUTPUT ${OUT}
    COMMAND ${GLSLC} -O -mfmt=c --target-env=vulkan1.0 -o ${OUT} ${SRC}
    DEPENDS ${SRC}
    COMMENT "Compilando shader ${SH}")
  list(APPEND SHADER_INCS ${OUT})
endforeach()
add_custom_target(retrogame_shaders DEPENDS ${SHADER_INCS})

add_library(retrogamesnes SHARED
  snes_core.cpp
  audio_out.cpp
  renderer.cpp
  emu_host.cpp)
add_dependencies(retrogamesnes retrogame_shaders)
target_include_directories(retrogamesnes PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${SHADER_OUT})
target_compile_definitions(retrogamesnes PRIVATE VK_USE_PLATFORM_ANDROID_KHR=1)
# -ffp-contract=off: sem "fma", para as contas de tempo darem exatamente o mesmo resultado do JS
target_compile_options(retrogamesnes PRIVATE -O3 -Wall -fno-strict-aliasing -ffp-contract=off)
target_link_libraries(retrogamesnes android log aaudio vulkan)

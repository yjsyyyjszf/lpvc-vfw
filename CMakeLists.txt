cmake_minimum_required(VERSION 3.14)

if(NOT WIN32)
  message(FATAL_ERROR "Windows build environment required.")
endif()


###############################################################################
# lpvc-vfw

set(PROJECT_NAME lpvc-vfw)
set(PROJECT_VERSION 0.1.0)

project(${PROJECT_NAME} VERSION ${PROJECT_VERSION})

add_library(${PROJECT_NAME} MODULE
  "src/dib_iterator.h"
  "src/drvproc.cpp"
  "src/resource.h"
  "src/resource.rc"
)

if(MINGW)
  target_sources(${PROJECT_NAME}
    PRIVATE
      "src/mingw.def"
  )
endif()

target_link_libraries(${PROJECT_NAME}
  liblpvc
  winmm.lib
)

set_target_properties(${PROJECT_NAME}
  PROPERTIES
    CXX_STANDARD 17
)


###############################################################################
# Warning configuration

if(MSVC)
  target_compile_definitions(${PROJECT_NAME}
    PRIVATE
      _CRT_SECURE_NO_WARNINGS
  )
endif()

if(MSYS)
  set_target_properties(${PROJECT_NAME}
    PROPERTIES
      PREFIX ""
      LINK_FLAGS "-static -static-libgcc -static-libstdc++"
  )
endif()

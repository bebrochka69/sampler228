# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "")
  file(REMOVE_RECURSE
  "CMakeFiles\\GrooveBoxUI_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\GrooveBoxUI_autogen.dir\\ParseCache.txt"
  "GrooveBoxUI_autogen"
  )
endif()

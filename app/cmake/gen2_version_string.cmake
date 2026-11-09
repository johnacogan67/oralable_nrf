# Zephyr's version parser cannot keep hyphens in EXTRAVERSION.
# Rewrite the two string macros after gen_version_h.cmake has filled the numbers.
if(NOT HEADER)
  message(FATAL_ERROR "HEADER is required")
endif()

file(READ "${HEADER}" text)
string(REGEX REPLACE
  "#define APP_VERSION_STRING[^\n]*"
  "#define APP_VERSION_STRING           \"2.0.0-gen2-nrfconnect\""
  text "${text}")
string(REGEX REPLACE
  "#define APP_VERSION_EXTENDED_STRING[^\n]*"
  "#define APP_VERSION_EXTENDED_STRING  \"2.0.0-gen2-nrfconnect+0\""
  text "${text}")
file(WRITE "${HEADER}" "${text}")


FIND_PATH(XLANG_INCLUDE_DIR NAMES "xlang.h"
                           PATHS ../xlang/Api
                           ENV INCLUDE DOC "Directory containing xlang.h include file")


IF (XLANG_INCLUDE_DIR)
   SET(XLANG_FOUND TRUE)
   get_filename_component(XLANG_DIR ${XLANG_INCLUDE_DIR} DIRECTORY)
    message(STATUS "XLANG_INCLUDE_DIR=${XLANG_INCLUDE_DIR},and XLANG=${XLANG_DIR}")
ELSE()
    message(STATUS "xlang: not found")
ENDIF (XLANG_INCLUDE_DIR)

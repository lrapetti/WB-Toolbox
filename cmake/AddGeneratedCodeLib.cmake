include(CMakeParseArguments)

macro(add_generated_code_lib)

# ==================================
# PARSE AND PROCESS MACRO PARAMETERS
# ==================================

set(_oneValueArgs MODELNAME SOURCE_FOLDER)
set(PREFIX "add_generated_code_lib")
string(TOUPPER ${PREFIX} PREFIX)

cmake_parse_arguments(${PREFIX}
    "${_options}"
    "${_oneValueArgs}"
    "${_multiValueArgs}"
    "${ARGN}")

# Check if the MODELNAME has been passed
set(SIMULINK_MODELNAME ${${PREFIX}_MODELNAME})
if(NOT DEFINED ${PREFIX}_MODELNAME OR ${PREFIX}_MODELNAME STREQUAL "")
    message(FATAL_ERROR "Model name not passed or empty")
endif()

## Try to find the folder containing the autogenerated sources
#if(NOT DEFINED ${PREFIX}_SOURCE_FOLDER)
#    get_filename_component(AUTOGEN_ABSPATH ${SIMULINK_MODELNAME}_grt_rtw ABSOLUTE)
#    if (NOT EXISTS ${AUTOGEN_ABSPATH})
#        message(FATAL_ERROR "Failed to find ${AUTOGEN_ABSPATH}. Use SOURCE_DIR argument \
#        if it is not contained in the current directory.")
#    endif()
#elseif(NOT ${${PREFIX}_SOURCE_FOLDER} STREQUAL "")
#    get_filename_component(AUTOGEN_ABSPATH ${${PREFIX}_SOURCE_FOLDER} ABSOLUTE)
#    if (NOT EXISTS ${AUTOGEN_ABSPATH})
#       message(FATAL_ERROR "Passed source directory ${${PREFIX}_SOURCE_FOLDER} does not exists.")
#    endif()
#else()
#    message(FATAL_ERROR "Failed to find folder containing autogenerated sources.")
#endif()

# If source folder argument was passed, assume to find here ${SIMULINK_MODELNAME}.cpp file
if(NOT DEFINED ${PREFIX}_SOURCE_FOLDER)
    get_filename_component(CPP_ABSPATH ${SIMULINK_MODELNAME}.cpp ABSOLUTE)
    if(NOT EXISTS ${CPP_ABSPATH})
        message(FATAL_ERROR "Failed to find ${SIMULINK_MODELNAME}.cpp. Use SOURCE_FOLDER argument \
        if it is not contained in the current directory.")
    endif()
    set(AUTOGEN_ABSPATH ${CMAKE_CURRENT_SOURCE_DIR})
else()
    get_filename_component(AUTOGEN_ABSPATH ${${PREFIX}_SOURCE_FOLDER} ABSOLUTE)
    # Check that the directory exists
    if(NOT EXISTS ${AUTOGEN_ABSPATH})
       message(FATAL_ERROR "Passed source directory ${${PREFIX}_SOURCE_FOLDER} does not exist.")
    endif()
    # Look for the cpp file
    if(NOT EXISTS ${AUTOGEN_ABSPATH}/${SIMULINK_MODELNAME}.cpp)
        message(FATAL_ERROR "Failed to find ${SIMULINK_MODELNAME}.cpp in ${${PREFIX}_SOURCE_FOLDER} directory.")
    endif()
endif()

# ====================================================
# CHECK THAT THE WBToolboxSimulinkCoder HAS BEEN FOUND
# ====================================================

if(NOT WBToolboxSimulinkCoder_FOUND)
    message(FATAL_ERROR "WBToolboxSimulinkCoder has not been found.\
    Use find_package before calling this macro.")
endif()

# ==============================================
# FIND THE defines.txt FILE AND LOAD ITS CONTENT
# ==============================================

# The folder containing the autogenerated sources should contain a defines.txt file
get_filename_component(CODER_DEFINES_FILE ${AUTOGEN_ABSPATH}/defines.txt ABSOLUTE)
if(EXISTS ${CODER_DEFINES_FILE})
    message(STATUS "Found autogenerated sources for ${SIMULINK_MODELNAME} model.")
else()
    message(FATAL_ERROR "Folder ${AUTOGEN_ABSPATH} does not contain defines.txt. It does not look \
    a valid folder containing autogenerated sources.")
endif()

# Parse the defines exported by Simulink Coder
file(STRINGS ${CODER_DEFINES_FILE} CODER_DEFINES)

# =============================
# SETUP THE INCLUDE DIRECTORIES
# =============================

# Model includes
set(CODER_INCLUDES "${AUTOGEN_ABSPATH}")

# System includes
set(CODER_INCLUDES_SYSTEM ""
    "${Matlab_ROOT_DIR}/extern/include"
    "${Matlab_ROOT_DIR}/simulink/include"
    "${Matlab_ROOT_DIR}/rtw/c/include"
    "${Matlab_ROOT_DIR}/rtw/c/src"
    "${Matlab_ROOT_DIR}/rtw/c/src/ext_mode/common"
)

# ==============
# LIBRARY TARGET
# ==============

# Simulink Coder Headers
# Here using GLOB is necessary because Simulink generates more or less headers depending
# on the Coder configuration of the model
file(GLOB CODER_HEADERS ${AUTOGEN_ABSPATH}/*.h)

# Simulink Coder source file
set(CODER_SOURCES "${AUTOGEN_ABSPATH}/${SIMULINK_MODELNAME}.cpp")

# Set the target name for the autogenerated sources
set(AUTOGEN_LIB "${SIMULINK_MODELNAME}_LIB")

add_library(${AUTOGEN_LIB} SHARED ${CODER_HEADERS} ${CODER_SOURCES})

target_compile_definitions(${AUTOGEN_LIB} PUBLIC ${CODER_DEFINES})
target_include_directories(${AUTOGEN_LIB} PUBLIC ${CODER_INCLUDES})
target_include_directories(${AUTOGEN_LIB} SYSTEM PUBLIC ${CODER_INCLUDES_SYSTEM})
target_link_libraries(${AUTOGEN_LIB} PUBLIC ${WBToolboxSimulinkCoder_LIBRARIES})

endmacro()

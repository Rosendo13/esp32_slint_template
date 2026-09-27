#=============================================================================
# ESP-IDF component wrapper around the pristine QP/C++ submodule in
# extern/qpcpp.
#
# Nothing in extern/qpcpp is modified. The ESP32-specific port lives in ./port/
# (see README.md), and ./port is placed FIRST on the include path so that
# QP/C++'s own sources -- which do #include "qp_port.hpp" -- pick up our port
# header instead of upstream's vanilla-FreeRTOS one.
#=============================================================================
cmake_minimum_required(VERSION 3.16)

# NOTE: COMPONENT_DIR, not CMAKE_CURRENT_SOURCE_DIR -- during IDF's early
# "component requirements" pass this file is included from a script whose
# CMAKE_CURRENT_SOURCE_DIR is the build directory.
set(QPCPP_PORT_DIR ${COMPONENT_DIR}/port)
set(QPCPP_DIR ${COMPONENT_DIR}/../../extern/qpcpp)

if(NOT EXISTS ${QPCPP_DIR}/include/qp.hpp)
    message(FATAL_ERROR
        "QP/C++ sources not found in ${QPCPP_DIR}. "
        "Run: git submodule update --init --recursive")
endif()

# Platform-independent QP/C++ sources. NOTE: src/qf/qf_actq.cpp is deliberately
# omitted -- the FreeRTOS port implements the active-object queue itself in
# port/qf_port.cpp.
set(qpcpp_src
    ${QPCPP_DIR}/src/qf/qep_hsm.cpp
    ${QPCPP_DIR}/src/qf/qep_msm.cpp
    ${QPCPP_DIR}/src/qf/qf_act.cpp
    ${QPCPP_DIR}/src/qf/qf_defer.cpp
    ${QPCPP_DIR}/src/qf/qf_dyn.cpp
    ${QPCPP_DIR}/src/qf/qf_mem.cpp
    ${QPCPP_DIR}/src/qf/qf_ps.cpp
    ${QPCPP_DIR}/src/qf/qf_qact.cpp
    ${QPCPP_DIR}/src/qf/qf_qeq.cpp
    ${QPCPP_DIR}/src/qf/qf_qmact.cpp
    ${QPCPP_DIR}/src/qf/qf_time.cpp
    ${QPCPP_PORT_DIR}/qf_port.cpp   # our port, not upstream's
)

set(qpcpp_inc
    ${QPCPP_PORT_DIR}   # MUST be first -- shadows upstream's port
    ${QPCPP_DIR}/include
    ${QPCPP_DIR}/src
    # qp_port.hpp includes "qp_config.hpp", which the application owns.
    # Referenced by path rather than REQUIRES because qp_app already
    # REQUIRES qpcpp.
    ${COMPONENT_DIR}/../qp_app/include
)

idf_component_register(
    SRCS ${qpcpp_src}
    INCLUDE_DIRS ${qpcpp_inc}
    REQUIRES freertos   # public: port/qp_port.hpp includes FreeRTOS.h
)

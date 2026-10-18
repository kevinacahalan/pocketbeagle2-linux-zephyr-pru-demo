cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(pb2_m4 C CXX)

# EmbeddedRPC 1.14.0, cloned by build.sh. It is not used as a Zephyr module:
# that module's Kconfig selects SERIAL from a symbol that depends on the full
# C++ library, and the loop fails configuration. The library sources are
# compiled here,
# with erpc_malloc/erpc_free on the Zephyr heap. Transport is the existing
# OpenAMP RPMsg endpoint, not the module's UART or RPMsg-Lite ports.
set(ERPC ${CMAKE_CURRENT_LIST_DIR}/../../third_party/erpc/erpc_c)

target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
  ${CMAKE_CURRENT_SOURCE_DIR}/../erpc/shim
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${ERPC}/infra
  ${ERPC}/port
  ${ERPC}/setup
  ${LIBMETAL_INCLUDE_DIR}
  ${OPENAMP_INCLUDE_DIR}
  ${PLATFORM_DIR}
)

target_sources(app PRIVATE
  src/main.c
  src/pb2_service.c
  src/pb2_erpc.cpp
  src/erpc_port_zephyr.cpp
  ../erpc/shim/c_pb2_server.cpp
  ../erpc/shim/pb2_server.cpp
  ../erpc/shim/pb2_interface.cpp
  ${ERPC}/infra/erpc_basic_codec.cpp
  ${ERPC}/infra/erpc_crc16.cpp
  ${ERPC}/infra/erpc_framed_transport.cpp
  ${ERPC}/infra/erpc_message_buffer.cpp
  ${ERPC}/infra/erpc_message_loggers.cpp
  ${ERPC}/infra/erpc_pre_post_action.cpp
  ${ERPC}/infra/erpc_server.cpp
  ${ERPC}/infra/erpc_simple_server.cpp
  ${ERPC}/infra/erpc_utils.cpp
  ${ERPC}/setup/erpc_server_setup.cpp
  ${ERPC}/setup/erpc_setup_mbf_dynamic.cpp
)

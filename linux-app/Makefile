CC = aarch64-linux-gnu-gcc
CXX = aarch64-linux-gnu-g++
CFLAGS = -O2 -Wall -Wextra -std=c11
CXXFLAGS = -O2 -Wall -Wextra -std=c++17 -fno-exceptions -fno-rtti

ROOT := $(abspath ..)
ERPC := $(ROOT)/third_party/erpc
SHIM := $(ROOT)/zephyr/erpc/shim
INCLUDES = -I. -I$(SHIM) -I$(ROOT)/zephyr/include \
	-I$(ERPC)/erpc_c/infra -I$(ERPC)/erpc_c/port \
	-I$(ERPC)/erpc_c/setup -I$(ERPC)/erpc_c/transports

ERPC_SRCS = \
	$(ERPC)/erpc_c/infra/erpc_basic_codec.cpp \
	$(ERPC)/erpc_c/infra/erpc_client_manager.cpp \
	$(ERPC)/erpc_c/infra/erpc_crc16.cpp \
	$(ERPC)/erpc_c/infra/erpc_framed_transport.cpp \
	$(ERPC)/erpc_c/infra/erpc_message_buffer.cpp \
	$(ERPC)/erpc_c/infra/erpc_message_loggers.cpp \
	$(ERPC)/erpc_c/infra/erpc_pre_post_action.cpp \
	$(ERPC)/erpc_c/infra/erpc_utils.cpp \
	$(ERPC)/erpc_c/port/erpc_port_stdlib.cpp \
	$(ERPC)/erpc_c/setup/erpc_client_setup.cpp \
	$(ERPC)/erpc_c/setup/erpc_setup_mbf_dynamic.cpp \
	$(SHIM)/c_pb2_client.cpp \
	$(SHIM)/pb2_client.cpp \
	$(SHIM)/pb2_interface.cpp \
	pb2_erpc_linux.cpp

all: pb2

pb2: pb2.o $(ERPC_SRCS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ pb2.o $(ERPC_SRCS)

pb2.o: pb2.c pb2_erpc_linux.h $(ROOT)/zephyr/include/pb2_shm.h
	$(CC) $(CFLAGS) -I. -I$(ROOT)/zephyr/include -c -o $@ pb2.c

clean:
	rm -f pb2 pb2.o

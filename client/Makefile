LIB60870_HOME=../../lib60870/lib60870-C

PROJECT_BINARY_NAME = i104_client
PROJECT_SOURCES = client.c
PROJECT_SOURCES += comm.c
PROJECT_SOURCES += i104.c

include $(LIB60870_HOME)/make/target_system.mk
include $(LIB60870_HOME)/make/stack_includes.mk

all:	$(PROJECT_BINARY_NAME)

include $(LIB60870_HOME)/make/common_targets.mk


$(PROJECT_BINARY_NAME):	$(PROJECT_SOURCES) $(LIB_NAME)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(PROJECT_BINARY_NAME) -I../include $(PROJECT_SOURCES) $(INCLUDES) $(LIB_NAME) $(LDLIBS)

clean:
	rm -f $(PROJECT_BINARY_NAME)



OBJECTS := $(patsubst %.c,%.o,$(wildcard *.c))
APPLICATION=lepton_controller
CFLAGS ?= -g -O0
CC=${CROSS_COMPILE}gcc

${APPLICATION}: ${OBJECTS}
	${CC} ${CFLAGS} -o $@ ${OBJECTS} ${LDFLAGS}

clean:
	rm -f *.o ${APPLICATION}

.PHONY: clean

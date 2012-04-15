all: test_mem

LIBDIR = ../lib
KERNELDIR = $(LIBDIR)/kernel
SOURCES = test_mem.c memalloc.c $(KERNELDIR)/list.c

test_mem: $(SOURCES)
	$(CC) -g -Wall -I$(LIBDIR) -I$(KERNELDIR) -o $@ $(SOURCES) -lpthread

clean:
	rm -rf test_mem *.o


LIBDIR=../lib
KERNELDIR=$(LIBDIR)/kernel
SOURCES2=listtest.c $(KERNELDIR)/list.c $(LIBDIR)/string.c
SOURCES3=listordered.c $(KERNELDIR)/list.c $(LIBDIR)/string.c

all: listtest listordered
listtest: $(SOURCES2)
	$(CC) -g -Wall -I$(LIBDIR) -I$(KERNELDIR) -o $@ $(SOURCES2) -lpthread

listordered: $(SOURCES3)
	$(CC) -g -Wall -I$(LIBDIR) -I$(KERNELDIR) -o $@ $(SOURCES3) -lpthread

clean:
	/bin/rm -rf listtest listordered *.o 

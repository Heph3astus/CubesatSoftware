
CXX=g++
INCLUDES=-I$(PIXELINK_SDK_INC)
LIBPATH=-L$(PIXELINK_SDK_LIB) 
DEFINES=-DPIXELINK_LINUX
LIBS=-lPxLApi
CFLAGS=-O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP $(DEFINES) $(INCLUDES)

SRCFILES=main.c
OBJFILES=$(SRCFILES:.c=.o)

all: recoverCamera

recoverCamera : $(OBJFILES) 
	rm -f $@
	$(CXX) $(LIBPATH) -o $@ $^ $(LIBS)

.c.o:
	$(CXX) $(CFLAGS) $< -o $@

clean:
	rm -rf *.o
	rm -rf recoverCamera



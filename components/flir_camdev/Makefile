INCLUDE += -I ../../lib/MemoryMappedObject/
LINK += 

CXXFLAGS += -Wall -c

LDFLAGS += $(INCLUDE)

bin/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDE) $< -o $@

bin/flir_camdev: bin/flir_camdev.o

build: bin/flir_camdev

clean:
	rm -rf bin/*

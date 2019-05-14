INCLUDE += -I ../../lib/MemoryMappedObject/ -I ../../lib/tclap/include/
LINK +=

CXXFLAGS += -Wall -c

LDFLAGS +=

bin/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDE) $< -o $@

bin/flir_camdev: bin/flir_camdev.o
	$(CXX) $(LDFLAGS) $(LINK) $< -o $@

build: bin/flir_camdev

clean:
	rm -rf bin/*

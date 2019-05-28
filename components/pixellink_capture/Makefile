INCLUDE += -I ../../lib/tclap/include/ -I ../../lib/Pixelink/include/
LINK += ../../lib/Pixelink/lib/libPxLApi.so

CXXFLAGS += -Wall -c

LDFLAGS +=

bin/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDE) $< -o $@

bin/pixellink_camera: bin/pixellink_camera.o bin/getsnapshot.o
	$(CXX) $(LDFLAGS) $(LINK) $< -o $@

build: bin/pixellink_camera

clean:
	rm -rf bin/*

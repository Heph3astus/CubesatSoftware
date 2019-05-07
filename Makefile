COMPONENTDIR ?= components
COMPONENTS := $(wildcard components/*)
CLEANDEPS := $(addprefix clean_, $(COMPONENTS))

component_%:
	make -C $(COMPONENTDIR)/$* build

clean_components/%:
	make -C $(COMPONENTDIR)/$* clean

build: component_flir_camdev

clean: $(CLEANDEPS)

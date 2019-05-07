#include <stdio.h>

#include "flir_camdev.h"
#include "MemoryMappedObject.h"

int main(int argc, char** argv) {
  MemoryMappedObject<struct flir_camdev> mmo("./test", CREATE);
}

#pragma once

#include <stdint.h>

#define CAM_WIDTH 160
#define CAM_HEIGHT 120

struct pixel {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

struct flir_camdev {
  struct pixel buf1[CAM_WIDTH][CAM_HEIGHT];
  struct pixel buf2[CAM_WIDTH][CAM_HEIGHT];
};

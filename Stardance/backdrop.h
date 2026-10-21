#pragma once
#include <stdint.h>

namespace Backdrop {

  void     init();
  void     setBlend(uint8_t artA, uint8_t artB, uint8_t t);
  uint16_t tint(int cy, uint8_t towardWhite);
  void     renderRows(uint16_t *strip, int stripTop, int y0, int y1);

}

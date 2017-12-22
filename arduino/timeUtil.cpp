#include "Arduino.h"
#include "./timeUtil.h"

unsigned long timeInterval(unsigned long from, unsigned long to) {
  if (from <= to) {
    return to - from;
  }
  else {
    return (4294967295 - from) + to;
  }
}

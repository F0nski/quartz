// Spike stub: no user data in flash on the FM-1 (the original reads STM32 flash).
// The engines that ask for user data fall back to their built-in tables.

#ifndef PLAITS_USER_DATA_H_
#define PLAITS_USER_DATA_H_

#include "stmlib/stmlib.h"

namespace plaits {

class UserData {
 public:
  enum { SIZE = 0x1000 };
  UserData() { }
  ~UserData() { }
  inline const uint8_t* ptr(int slot) const { return NULL; }
};

}  // namespace plaits

#endif  // PLAITS_USER_DATA_H_

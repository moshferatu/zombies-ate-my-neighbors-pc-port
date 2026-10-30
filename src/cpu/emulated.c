// The build with LakeSnes's 65816: it runs whatever no port does.
#include "cpu/kind.h"

bool cpu_emulated(void) { return true; }

void cpu_on_unported(CpuUnportedFn fn) { (void)fn; }

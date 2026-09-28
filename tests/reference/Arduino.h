// Minimal Arduino shim so the original KeeloqLib compiles on the host as a test oracle.
#pragma once

#define bitRead(value, bit) (((value) >> (bit)) & 0x01)

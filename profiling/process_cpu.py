"""Read the POSIX process CPU clock (all threads, user and kernel execution)."""
import ctypes
import time

class ProcessCPU:
    def __init__(self, pid):
        libc = ctypes.CDLL(None)
        libc.clock_getcpuclockid.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_int)]
        clock = ctypes.c_int()
        error = libc.clock_getcpuclockid(pid, ctypes.byref(clock))
        if error: raise OSError(error, 'cannot read compositor process CPU clock')
        self.clock = clock.value
        self.resolution = time.clock_getres(self.clock)

    def read(self):
        return time.clock_gettime_ns(self.clock)

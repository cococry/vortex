"""Scoped X11 idle inhibition; leaves the user's timeout settings unchanged."""
import ctypes as C

class HostDisplay:
    def __init__(self):
        self.x = C.CDLL('libX11.so.6')
        self.x.XOpenDisplay.restype = C.c_void_p
        self.x.XCloseDisplay.argtypes = [C.c_void_p]
        self.x.XSync.argtypes = [C.c_void_p, C.c_int]
        self.x.XResetScreenSaver.argtypes = [C.c_void_p]
        self.s = C.CDLL('libXss.so.1')
        self.s.XScreenSaverSuspend.argtypes = [C.c_void_p, C.c_int]
        self.s.XScreenSaverQueryVersion.argtypes = [C.c_void_p, C.POINTER(C.c_int), C.POINTER(C.c_int)]
        self.e = C.CDLL('libXext.so.6')
        self.e.DPMSCapable.argtypes = [C.c_void_p]
        self.e.DPMSInfo.argtypes = [C.c_void_p, C.POINTER(C.c_ushort), C.POINTER(C.c_ubyte)]
        self.e.DPMSForceLevel.argtypes = [C.c_void_p, C.c_ushort]
        self.display = self.x.XOpenDisplay(None)
        if not self.display: raise RuntimeError('cannot inhibit host X11 idle')
        major=C.c_int();minor=C.c_int()
        if not self.s.XScreenSaverQueryVersion(self.display,C.byref(major),C.byref(minor)) or (major.value,minor.value)<(1,1):
            self.x.XCloseDisplay(self.display);self.display=None
            raise RuntimeError('host needs XScreenSaver 1.1 for scoped idle inhibition')
        self.s.XScreenSaverSuspend(self.display, 1)
        self.x.XResetScreenSaver(self.display)
        if self.e.DPMSCapable(self.display):
            level = C.c_ushort(); enabled = C.c_ubyte()
            if self.e.DPMSInfo(self.display, C.byref(level), C.byref(enabled)) and enabled.value:
                self.e.DPMSForceLevel(self.display, 0)
        self.x.XSync(self.display, 0)
        self.check()

    def check(self):
        if self.e.DPMSCapable(self.display):
            level = C.c_ushort(); enabled = C.c_ubyte()
            if not self.e.DPMSInfo(self.display, C.byref(level), C.byref(enabled)):
                raise RuntimeError('cannot query host DPMS state')
            if enabled.value and level.value != 0:
                raise RuntimeError('host display powered off during acquisition')

    def close(self):
        if self.display:
            self.s.XScreenSaverSuspend(self.display, 0)
            self.x.XCloseDisplay(self.display)
            self.display = None

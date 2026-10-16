"""Connect AF_UNIX on Windows Python builds lacking the address converter.

Winsock still supports AF_UNIX. Only connect needs ctypes; the returned socket
uses Python's ordinary send/recv/select and retains the client's timeout model.
"""
import os
import socket


def connect_unix(path):
    if hasattr(socket, "AF_UNIX"):
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            sock.connect(path)
            return sock
        except BaseException:
            sock.close()
            raise
    if os.name != "nt":
        raise OSError("this Python build has no AF_UNIX support")
    import ctypes
    class SockAddrUnix(ctypes.Structure):
        _fields_ = [("family", ctypes.c_ushort), ("path", ctypes.c_char*108)]
    encoded = os.fsencode(path)
    if not encoded or len(encoded) >= 108 or b"\0" in encoded:
        raise ValueError("AF_UNIX path must contain 1..107 bytes without NUL")
    api = ctypes.WinDLL("ws2_32")
    api.connect.argtypes = [ctypes.c_size_t, ctypes.c_void_p, ctypes.c_int]
    api.connect.restype = ctypes.c_int
    api.WSAGetLastError.restype = ctypes.c_int
    sock = socket.socket(1, socket.SOCK_STREAM, 0) # AF_UNIX = 1 in Winsock
    try:
        address = SockAddrUnix(1, encoded)
        if api.connect(sock.fileno(), ctypes.byref(address), ctypes.sizeof(address)):
            error = api.WSAGetLastError()
            raise OSError(error, f"AF_UNIX connect failed: {path}")
        return sock
    except BaseException:
        sock.close()
        raise

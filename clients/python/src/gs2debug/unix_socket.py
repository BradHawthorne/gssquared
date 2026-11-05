"""Connect AF_UNIX on Windows Python builds lacking the address converter.

Winsock still supports AF_UNIX. Only connect needs ctypes; the returned socket
uses Python's ordinary send/recv/select and retains the client's timeout model.
"""
import os
import socket

# sockaddr_un.sun_path is 108 bytes on Windows and Linux, NUL included.
MAX_PATH_BYTES = 107


def check_path(path):
    """Refuse a socket path that cannot fit sun_path, naming the limit and the fix.

    Paths under the temporary directory are the usual cause: a long TEMP/TMP
    (Windows) or TMPDIR plus a per-run directory and a file name passes 107 bytes,
    and the failure would otherwise surface as a bind or connect error far away.
    """
    encoded = os.fsencode(path)
    if not encoded or b"\0" in encoded:
        raise ValueError("AF_UNIX path must contain 1..%d bytes without NUL" % MAX_PATH_BYTES)
    if len(encoded) > MAX_PATH_BYTES:
        raise ValueError(
            "AF_UNIX socket path is %d bytes; the limit is %d (sun_path): %s -- use a shorter directory "
            "(for a path under the temporary directory, set TEMP/TMP or TMPDIR to a shorter one)"
            % (len(encoded), MAX_PATH_BYTES, path))
    return encoded


def connect_unix(path):
    encoded = check_path(path)
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

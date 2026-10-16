"""Protocol error raised when the server returns an ERROR frame."""

from __future__ import annotations


class ProtocolError(Exception):
    def __init__(self, code: int, message: str = "") -> None:
        self.code = code
        self.message = message
        super().__init__(f"protocol error {code}: {message}" if message else f"protocol error {code}")


class RequestPending(TimeoutError):
    """A complete request was sent; its outcome has not yet been received.

    Recover with wait_response() or wait_agent_command(), never by reissuing.
    """
    def __init__(self,sequence,request_type):
        self.sequence=sequence;self.request_type=request_type
        super().__init__(f'request {sequence} (0x{request_type:08x}) pending; wait for its original reply; timeout is not cancellation')

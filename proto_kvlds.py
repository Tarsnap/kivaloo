# proto_kvlds.py
from .wire import Wire, make_packet

class Proto_kvlds:
    def __init__(self, sock):
        self.wire = Wire(sock)
    def get(self, key):
        req = bytes([len(key)]) + key
        self.wire.send(req)
        msgnum, payload = self.wire.recv()
        resp = Response(payload)
        status = resp.get_uint32()
        if status != 0:
            return (status, None)
        value = resp.get_kvlds_data()
        return (status, value)
    def range(self, max, start, end):
        req = struct.pack('>I', max) + bytes([len(start)]) + start + bytes([len(end)]) + end
        self.wire.send(req)
        msgnum, payload = self.wire.recv()
        resp = Response(payload)
        next_key = resp.get_kvlds_nextkey()
        pairs = {}
        while resp.remaining() > 0:
            k = resp.get_kvlds_data()
            v = resp.get_kvlds_data()
            pairs[k] = v
        return (next_key, pairs)

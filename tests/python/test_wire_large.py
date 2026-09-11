#!/usr/bin/env python3

"""Regression tests for issue #369: responses larger than 16 KiB.

The Python wire client used to read a response with a single
``sock.recv(MAX_RESPONSE_BYTES)`` (16384) call, so any valid response
record longer than 16364 bytes (frame = record + 20 bytes) failed with
``struct.error``.  A legal LBS GET response is 4 + blklen bytes with
blklen up to 131072, and KVLDS RANGE responses are dynamically sized,
so both protocols can legitimately exceed the old ceiling.

Run from the repository root with crcmod installed:
    python3 -m unittest discover -s tests/python -p "test_wire_large.py" -v
No kivaloo server or external service is required.
"""

from collections import deque
import struct
import unittest

from kivaloo import wire


# Boundaries of the old (buggy) implementation.
OLD_MAX_FRAME = 16384
OLD_MAX_RECORD = OLD_MAX_FRAME - 20  # 16364

# Largest legal LBS GET response record: 4 (status) + 131072 (max blklen).
# See PROTO_LBS_BLKLEN_MAX in lib/proto_lbs/proto_lbs.h.
LBS_MAX_GET_RECORD = 4 + 131072


class ChunkedSocket(object):
    """A stream socket mock returning data in fixed fragments."""

    def __init__(self, chunks):
        self.chunks = deque(chunks)
        self.sent = []

    def sendall(self, data):
        self.sent.append(data)

    def recv(self, size):
        if not self.chunks:
            return b''
        chunk = self.chunks.popleft()
        result, rest = chunk[:size], chunk[size:]
        if rest:
            self.chunks.appendleft(rest)
        return result

    def close(self):
        pass


def chunked(packet, size=1):
    """Split a packet into fragments of at most size bytes."""
    return [packet[i:i + size] for i in range(0, len(packet), size)]


class WireLargeResponseTests(unittest.TestCase):
    def client(self, sock):
        client = wire.Wire.__new__(wire.Wire)
        client.msgnum = 0
        client.sock = sock
        self.addCleanup(client.close)
        return client

    def roundtrip(self, record, fragments):
        client = self.client(ChunkedSocket(fragments))
        response = client.send_recv('>I', 0x00)
        self.assertEqual(response.data, record)
        return response

    def test_small_response(self):
        record = struct.pack('>IQ', 32768, 0)
        self.roundtrip(record, [wire.make_packet(0, record)])

    def test_record_exactly_at_old_limit(self):
        record = b'x' * OLD_MAX_RECORD
        packet = wire.make_packet(0, record)
        assert len(packet) == OLD_MAX_FRAME
        self.roundtrip(record, [packet[:16], packet[16:]])

    def test_record_just_above_old_limit(self):
        record = b'x' * (OLD_MAX_RECORD + 1)
        packet = wire.make_packet(0, record)
        assert len(packet) == OLD_MAX_FRAME + 1
        self.roundtrip(record, [packet])

    def test_lbs_get_32kib_block(self):
        # GET response record: 4-byte status + blklen bytes of block data.
        blklen = 32768
        record = struct.pack('>I', 0) + bytes(range(256)) * (blklen // 256)
        assert len(record) == 4 + blklen
        packet = wire.make_packet(0, record)
        self.roundtrip(record, [packet[:16], packet[16:]])

    def test_lbs_get_max_block(self):
        record = struct.pack('>I', 0) + b'y' * 131072
        assert len(record) == LBS_MAX_GET_RECORD
        packet = wire.make_packet(0, record)
        # Fragment the body to also cover short stream reads.
        self.roundtrip(record, [packet[:7], packet[7:16], packet[16:1000],
                                packet[1000:]])

    def test_large_response_one_byte_reads(self):
        record = b'z' * 20000
        packet = wire.make_packet(0, record)
        client = self.client(ChunkedSocket(chunked(packet, 1)))
        self.assertEqual(client.send_recv('>I', 0x00).data, record)

    def test_fragmented_header(self):
        record = b'data'
        packet = wire.make_packet(0, record)
        for split in (1, 8, 15):
            with self.subTest(split=split):
                self.roundtrip(record, [packet[:split], packet[split:]])

    def test_premature_eof(self):
        packet = wire.make_packet(0, b'x' * 20000)
        for length in (0, 1, 15, 16, 20, len(packet) - 1):
            with self.subTest(length=length):
                client = self.client(ChunkedSocket([packet[:length]]))
                with self.assertRaises(EOFError):
                    client.send_recv('>I', 0x00)

    def test_oversized_length_with_no_body(self):
        # A header with a valid checksum but an absurd record_length and
        # no following body must fail fast with EOFError: _recv_exact only
        # accumulates bytes actually received, so no giant buffer is
        # allocated and the client does not hang waiting for 4 GiB.
        header12 = struct.pack('>QI', 0, 0xffffffff)
        header = header12 + struct.pack('>I', wire._checksum(header12))
        client = self.client(ChunkedSocket([header]))
        with self.assertRaises(EOFError):
            client.send_recv('>I', 0x00)

    def test_invalid_header_checksum(self):
        packet = bytearray(wire.make_packet(0, b'x' * 20000))
        packet[5] ^= 1
        client = self.client(ChunkedSocket([bytes(packet)]))
        with self.assertRaisesRegex(Exception, 'header checksums'):
            client.send_recv('>I', 0x00)

    def test_invalid_record_checksum(self):
        packet = bytearray(wire.make_packet(0, b'x' * 20000))
        packet[-1] ^= 1
        client = self.client(ChunkedSocket([bytes(packet)]))
        with self.assertRaisesRegex(Exception, 'record checksums'):
            client.send_recv('>I', 0x00)

    def test_wrong_response_id(self):
        client = self.client(ChunkedSocket([wire.make_packet(9, b'data')]))
        with self.assertRaises(AssertionError):
            client.send_recv('>I', 0x00)

    def test_consecutive_large_frames(self):
        first = b'a' * 20000
        second = b'b' * 30000
        client = self.client(ChunkedSocket([wire.make_packet(0, first)
                                            + wire.make_packet(1, second)]))
        self.assertEqual(client.send_recv('>I', 0x00).data, first)
        self.assertEqual(client.send_recv('>I', 0x00).data, second)


if __name__ == '__main__':
    unittest.main()

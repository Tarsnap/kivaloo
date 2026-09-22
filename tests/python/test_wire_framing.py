#!/usr/bin/env python3

"""Local tests of complete-frame reads in the Python wire client.

Run from the repository root with crcmod installed:
    python3 -m unittest discover -s tests/python -p test_wire_framing.py -v
No kivaloo server or external service is required.
"""

from collections import deque
import socket
import struct
import threading
import unittest

from kivaloo import wire


class ChunkedSocket:
    """A stream whose available chunks need not match protocol frames."""
    def __init__(self, chunks):
        self.chunks = deque(chunks)
        self.sent = []
        self.read_sizes = []

    def sendall(self, data):
        self.sent.append(data)

    def recv(self, size):
        self.read_sizes.append(size)
        if not self.chunks:
            return b''
        chunk = self.chunks.popleft()
        result, rest = chunk[:size], chunk[size:]
        if rest:
            self.chunks.appendleft(rest)
        return result

    def close(self):
        pass


class ObservedSocket:
    """Tell a local peer when the first real socket read has returned."""
    def __init__(self, sock, first_read):
        self.sock = sock
        self.first_read = first_read

    def sendall(self, data):
        self.sock.sendall(data)

    def recv(self, size):
        result = self.sock.recv(size)
        self.first_read.set()
        return result

    def close(self):
        self.sock.close()


def read_exact(sock, size):
    """Read the synthetic peer's request, independently of Wire.send_recv."""
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise EOFError('Local peer closed early')
        data.extend(chunk)
    return bytes(data)


class WireFramingTests(unittest.TestCase):
    def client(self, sock):
        client = wire.Wire.__new__(wire.Wire)
        client.msgnum = 0
        client.sock = sock
        self.addCleanup(client.close)
        return client

    def test_checksum_matches_c_reference(self):
        # From libcperciva/alg/CRC32C.c's hardware/software test vector.
        self.assertEqual(wire._checksum(b'hello world'), 0xca130baa)

    def test_complete_response(self):
        record = struct.pack('>II', 255, 255)
        sock = ChunkedSocket([wire.make_packet(0, record)])
        client = self.client(sock)
        self.assertEqual(client.send_recv('>I', 0x100).data, record)
        self.assertEqual(wire.split_packet(sock.sent[0]),
                         (0, struct.pack('>I', 0x100)))

    def test_every_response_split(self):
        record = struct.pack('>II', 255, 255)
        packet = wire.make_packet(0, record)
        for split in range(1, len(packet)):
            with self.subTest(split=split):
                client = self.client(ChunkedSocket(
                    [packet[:split], packet[split:]]))
                self.assertEqual(client.send_recv('>I', 0x100).data, record)

    def test_one_byte_reads(self):
        record = b'a response containing more than one byte'
        packet = wire.make_packet(0, record)
        client = self.client(ChunkedSocket(
            [bytes([byte]) for byte in packet]))
        self.assertEqual(client.send_recv('>I', 0x100).data, record)

    def test_empty_record(self):
        client = self.client(ChunkedSocket([wire.make_packet(0, b'')]))
        self.assertEqual(client.send_recv('>I', 0x100).data, b'')

    def test_exact_response_limit(self):
        record = b'x' * (wire.MAX_RESPONSE_BYTES - 20)
        packet = wire.make_packet(0, record)
        client = self.client(ChunkedSocket(
            [packet[:16], packet[16:100], packet[100:]]))
        self.assertEqual(client.send_recv('>I', 0x100).data, record)

    def test_oversized_response_rejected_before_body_read(self):
        packet = wire.make_packet(0, b'x' * (wire.MAX_RESPONSE_BYTES - 19))
        sock = ChunkedSocket([packet[:16], packet[16:]])
        client = self.client(sock)
        with self.assertRaisesRegex(ValueError, 'response.*large'):
            client.send_recv('>I', 0x100)
        self.assertEqual(sock.read_sizes, [16])

    def test_invalid_header_rejected_before_body_read(self):
        packet = bytearray(wire.make_packet(0, b'data'))
        packet[15] ^= 1
        sock = ChunkedSocket([bytes(packet[:16]), bytes(packet[16:])])
        client = self.client(sock)
        with self.assertRaisesRegex(Exception, 'header checksums'):
            client.send_recv('>I', 0x100)
        self.assertEqual(sock.read_sizes, [16])

    def test_invalid_record_checksum_rejected(self):
        packet = bytearray(wire.make_packet(0, b'data'))
        packet[-1] ^= 1
        client = self.client(ChunkedSocket([bytes(packet)]))
        with self.assertRaisesRegex(Exception, 'record checksums'):
            client.send_recv('>I', 0x100)

    def test_eof_in_header_or_body(self):
        packet = wire.make_packet(0, struct.pack('>II', 255, 255))
        for length in (0, 1, 8, 15, 16, 20, len(packet) - 1):
            with self.subTest(length=length):
                client = self.client(ChunkedSocket([packet[:length]]))
                with self.assertRaises(EOFError):
                    client.send_recv('>I', 0x100)

    def test_reads_only_one_frame_at_a_time(self):
        first = wire.make_packet(0, b'first')
        second = wire.make_packet(1, b'second')
        client = self.client(ChunkedSocket([first + second]))
        self.assertEqual(client.send_recv('>I', 0x100).data, b'first')
        self.assertEqual(client.send_recv('>I', 0x100).data, b'second')

    def test_response_id_is_still_checked(self):
        client = self.client(ChunkedSocket([wire.make_packet(9, b'data')]))
        with self.assertRaises(AssertionError):
            client.send_recv('>I', 0x100)

    def test_real_local_socket_response_in_two_fragments(self):
        # No timing sleeps: the peer waits until recv has returned the first
        # eight bytes before it sends the rest of a valid PARAMS response.
        client_sock, peer_sock = socket.socketpair()
        self.addCleanup(peer_sock.close)
        client_sock.settimeout(3)
        peer_sock.settimeout(3)
        first_read = threading.Event()
        client = self.client(ObservedSocket(client_sock, first_read))
        record = struct.pack('>II', 255, 255)
        packet = wire.make_packet(0, record)
        errors = []

        def peer():
            try:
                request = read_exact(peer_sock, 24)
                self.assertEqual(wire.split_packet(request),
                                 (0, struct.pack('>I', 0x100)))
                peer_sock.sendall(packet[:8])
                if not first_read.wait(3):
                    raise TimeoutError('Client did not consume first fragment')
                peer_sock.sendall(packet[8:])
            except Exception as error:
                errors.append(error)

        thread = threading.Thread(target=peer, daemon=True)
        thread.start()
        try:
            self.assertEqual(client.send_recv('>I', 0x100).get('>II'),
                             (255, 255))
        finally:
            thread.join(4)
            self.assertFalse(thread.is_alive(), 'Local peer did not finish')
            self.assertEqual(errors, [])


if __name__ == '__main__':
    unittest.main()

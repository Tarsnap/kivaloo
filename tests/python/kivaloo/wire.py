#!/usr/bin/env python3

""" Implentation of the kivaloo Wire protocol. """

import socket
import struct

import crcmod


# ## Public constant
# This is a somewhat arbitrary value
MAX_RESPONSE_BYTES = 16384

# Set up CRC32c algorithm
_CASTAGNOLI = 0x11EDC6F41    # From kivaloo/INTERFACES
_ILONGATSAC = 0x82f63b78     # Bit-reversed CASTAGNOLI polynomial
_CRC_FUNC = crcmod.mkCrcFun(_CASTAGNOLI, initCrc=_ILONGATSAC, rev=True)


def _checksum(data):
    """ Calculate the reversed CRC32C checksum on the data. """
    crc = _CRC_FUNC(data)
    # Swap bytes in that 32-bit value (which shouldn't be interpreted as a
    # single number)
    out = 0
    for pos in [0, 8, 16, 24]:
        hexit = (crc >> pos) & 0xff
        out += hexit << (24 - pos)
    return out


def make_packet(idnum, record):
    """ Create a Wire packet from a record. """
    record_length = len(record)

    # Calculate CRC32c on first 12 bytes
    crc_header = _checksum(struct.pack('>QI', idnum, record_length))

    # Calculate CRC32c on record, xor CRC32c on first 12 bytes
    crc_record = _checksum(record) ^ crc_header

    # Construct packet
    packet = struct.pack('>QII%dsI' % record_length,
                         idnum, record_length, crc_header, record, crc_record)
    return packet


def split_packet(packet):
    """ Split a Wire packet into a record. """
    idnum, record_length, crc_recv = struct.unpack_from('>QII', packet)

    # Check CRC32 on first 12 bytes
    crc_first = _checksum(struct.pack('>QI', idnum, record_length))
    if crc_recv != crc_first:
        raise Exception("Mismatch in header checksums")

    # Get main message.  16 = 8+4+4 = len(QII).
    record, crc = struct.unpack_from('>%dsI' % record_length,
                                     packet, offset=16)

    # Sanity checks
    assert len(packet) == len(record) + 16 + 4
    crc_record = _checksum(struct.pack('>%ds' % record_length, record))
    if crc != (crc_record ^ crc_first):
        raise Exception("Mismatch in record checksums")

    return idnum, record


class Response(object):
    """ The response from the kivaloo server. """
    def __init__(self, data):
        self.data = data
        self.offset = 0

    def remaining(self):
        """ For debugging: get the remaining (unprocessed) bytes. """
        return self.data[self.offset:]

    def get(self, fmt):
        """ Get some data from the response.  Can be run multiple times;
            automatically tracks how many bytes have already been extracted
            and sets the offset accordingly.

            fmt: a format string as per python's struct module, such as
                 ">IQ".  Relevant extract from the python docs:
                     >   -- little-endian
                     B   -- unsigned 1-byte int
                     I   -- unsigned 4-byte int
                     Q   -- unsigned 8-byte int
                     s   -- 1-byte char[]
                     *s  -- an *-byte char[]
        """
        values = struct.unpack_from(fmt, self.data, offset=self.offset)
        self.offset += struct.calcsize(fmt)
        return values

    def get_int(self):
        """ Get a 4-byte integer. """
        data, = struct.unpack_from('>I', self.data, offset=self.offset)
        self.offset += 4
        return data

    def get_kvlds_data(self):
        """ Get data from the standard kivaloo "length and data" format:
                [1 byte length][X byte data]

            If the length is 0, return None.
        """
        length, = struct.unpack_from('>B', self.data, offset=self.offset)
        self.offset += 1
        if length == 0:
            return None
        data, = struct.unpack_from('>%ds' % length,
                                   self.data, offset=self.offset)
        self.offset += length
        return data


class Wire(object):
    """ Base class for the kivaloo Wire protocol. """
    def __init__(self, socket_name):
        """ Connect to a server. """
        self.msgnum = 0
        self.sock = socket.socket(socket.AF_UNIX)
        self.sock.connect(socket_name)

    def __del__(self):
        self.close()

    def close(self):
        """ Close a connection to a server. """
        if self.sock:
            self.sock.close()
        self.sock = None

    def send(self, record):
        """ Send a message. """
        data = make_packet(self.msgnum, record)
        self.msgnum += 1
        self.sock.sendall(data)

    def send_recv(self, *msg):
        """ Send a message and receive the response (blocking).  Supports
            responses up to wire.MAX_RESPONSE_BYTES.

            msg: the message in a format suitable for python's struct.pack().
        """
        # Use the variable number of arguments
        record = struct.pack(*msg)

        # Construct a wire packet
        data = make_packet(self.msgnum, record)
        self.msgnum += 1
        self.sock.sendall(data)

        # Wait for a response
        idnum, record = split_packet(self.sock.recv(MAX_RESPONSE_BYTES))
        assert idnum == self.msgnum - 1

        return Response(record)

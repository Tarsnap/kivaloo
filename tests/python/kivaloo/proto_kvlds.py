#!/usr/bin/env python3

""" Implentation of the kivaloo communication to KVLDS. """

import struct

import kivaloo.wire


class NotBinaryData(TypeError):
    """ You must pass some `bytes` to this function. """
    pass


class KivalooTooMuchData(TypeError):
    """ Kivaloo can only handle keys and values up to length 255. """
    pass


def kvlds_length_and_bytes(data):
    """ Make some bytes storing the length and data in the kvlds format. """
    # Sanity check
    if not isinstance(data, bytes):
        raise NotBinaryData()
    if len(data) > 255:
        raise KivalooTooMuchData()

    # Return form expected by kivaloo
    return struct.pack('>B%ds' % (len(data)), len(data), data)


class Proto_kvlds():
    """ Wire protocol to a kvlds server.  For simplicity, communicates with
        the server with blocking functions (although the protocol itself is
        non-blocking).
    """
    def __init__(self):
        super().__init__()
        self.wire = kivaloo.wire.Wire(kivaloo.servers.KVLDS_SOCK)
        self.max_length, self.max_value = self.params()

    def close(self):
        """ Close the connection.  Not necessary to run explicitly. """
        self.wire.close()
        self.wire = None

    def params(self):
        """ Get the block size and next free block. """
        reply = self.wire.send_recv('>I', 0x100)
        max_length, max_value = reply.get('>II')
        return max_length, max_value

    def set(self, key, value):
        """ Set a key/value pair. """
        kl_key = kvlds_length_and_bytes(key)
        kl_value = kvlds_length_and_bytes(value)
        reply = self.wire.send_recv('>I%ds%ds' % (len(kl_key), len(kl_value)),
                                    0x110, kl_key, kl_value)
        status = reply.get_int()
        return status

    def cas(self, key, oval, value):
        """ Compare-and-swap. """
        kl_key = kvlds_length_and_bytes(key)
        kl_oval = kvlds_length_and_bytes(oval)
        kl_value = kvlds_length_and_bytes(value)
        reply = self.wire.send_recv('>I%ds%ds%ds' % (len(kl_key),
                                                     len(kl_oval),
                                                     len(kl_value)),
                                    0x111, kl_key, kl_oval, kl_value)
        status = reply.get_int()
        return status

    def add(self, key, value):
        """ Add a key/value pair. """
        kl_key = kvlds_length_and_bytes(key)
        kl_value = kvlds_length_and_bytes(value)
        reply = self.wire.send_recv('>I%ds%ds' % (len(kl_key), len(kl_value)),
                                    0x112, kl_key, kl_value)
        status = reply.get_int()
        return status

    def modify(self, key, value):
        """ Modify a key / value pair. """
        kl_key = kvlds_length_and_bytes(key)
        kl_value = kvlds_length_and_bytes(value)
        reply = self.wire.send_recv('>I%ds%ds' % (len(kl_key), len(kl_value)),
                                    0x113, kl_key, kl_value)
        status = reply.get_int()
        return status

    def delete(self, key):
        """ Delete a key / value pair. """
        kl_key = kvlds_length_and_bytes(key)
        reply = self.wire.send_recv('>I%ds' % (len(kl_key)),
                                    0x120, kl_key)
        status = reply.get_int()
        return status

    def cad(self, key, oval):
        """ Compare and delete. """
        kl_key = kvlds_length_and_bytes(key)
        kl_oval = kvlds_length_and_bytes(oval)
        reply = self.wire.send_recv('>I%ds%ds' % (len(kl_key), len(kl_oval)),
                                    0x121, kl_key, kl_oval)
        status = reply.get_int()
        return status

    def get(self, key):
        """ Get a value. """
        kl_key = kvlds_length_and_bytes(key)
        reply = self.wire.send_recv('>I%ds' % (len(kl_key)), 0x130, kl_key)
        status = reply.get_int()
        if status == 1:
            return status, None
        value = reply.get_kvlds_data()
        return status, value

    def range(self, maxsize, start, end):
        """ Get a range of key/value pairs. """
        kl_start = kvlds_length_and_bytes(start)
        kl_end = kvlds_length_and_bytes(end)
        reply = self.wire.send_recv('>II%ds%ds' % (len(kl_start), len(kl_end)),
                                    0x131, maxsize, kl_start, kl_end)
        status = reply.get_int()
        if status == 1:
            return status, None, None

        num_pairs = reply.get_int()
        nextkey = reply.get_kvlds_data()

        pairs = {}
        for _ in range(num_pairs):
            key = reply.get_kvlds_data()
            val = reply.get_kvlds_data()
            pairs[key] = val

        return status, nextkey, pairs

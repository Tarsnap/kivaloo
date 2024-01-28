#!/usr/bin/env python3

""" Implentation of the kivaloo communication to LBS. """

import kivaloo.wire
import kivaloo.servers


class NotBinaryData(TypeError):
    """ You must pass some `bytes` to this function. """
    pass


class Proto_lbs():
    """ Wire protocol to a lbs server.  For simplicity, communicates with
        the server with blocking functions (although the protocol itself is
        non-blocking).
    """
    def __init__(self, socketname=None):
        super().__init__()
        if not socketname:
            socketname = kivaloo.servers.LBS_SOCK
        self.wire = kivaloo.wire.Wire(socketname)
        self.block_size, _ = self.params()

    def close(self):
        """ Close the connection.  Not necessary to run explicitly. """
        self.wire.close()
        self.wire = None

    def params(self):
        """ Get the block size and next free block. """
        reply = self.wire.send_recv('>I', 0x00)
        block_size, next_block = reply.get('>IQ')
        return block_size, next_block

    def params2(self):
        """ Get the block size, next free block, and the last used block. """
        reply = self.wire.send_recv('>I', 0x04)
        block_size, next_block, last_block = reply.get('>IQQ')
        return block_size, next_block, last_block

    def get(self, blocknum):
        """ Get data from a block. """
        reply = self.wire.send_recv('>IQ', 0x01, blocknum)
        status = reply.get_int()
        if status == 1:
            return status, None
        data, = reply.get('>%ds' % (self.block_size))
        return status, data

    def append(self, nums, start, blocks):
        """ Add data to the server. """
        if not isinstance(blocks, bytes):
            raise NotBinaryData()
        if len(blocks) % self.block_size != 0:
            raise Exception("wire append: Invalid blocksize: %d" % len(blocks))
        num_bytes = self.block_size * nums
        reply = self.wire.send_recv('>IIQ%ds' % (num_bytes),
                                    0x02, nums, start, blocks)
        status = reply.get_int()
        if status == 1:
            return status, None
        next_block, = reply.get('>Q')
        return status, next_block

    def free(self, keep_num):
        """ Indicate that some data can be freed.  The server can decide
            if/when to free those blocks.
        """
        reply = self.wire.send_recv('>IQ', 0x03, keep_num)
        status = reply.get_int()
        return status

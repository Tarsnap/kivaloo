#!/usr/bin/env python3

""" Display the contents of a kivaloo KVLDS server. """

import kivaloo.servers
import kivaloo.proto_kvlds


def main():
    """ Dump the contents of a KVLDS server to stdout. """
    lbs = kivaloo.servers.Server_lbs()
    kvlds = kivaloo.servers.Server_kvlds()
    proto_kvlds = kivaloo.proto_kvlds.Proto_kvlds()

    maxsize = 8192
    nextkey = "".encode()
    end = "".encode()

    while nextkey is not None:
        status, nextkey, pairs = proto_kvlds.range(maxsize, nextkey, end)
        assert status == 0
        for key, val in pairs.items():
            print("\t%s\t%s" % (key.decode(), val.decode()))

    proto_kvlds.close()
    kvlds.shutdown()
    lbs.shutdown()


if __name__ == "__main__":
    main()

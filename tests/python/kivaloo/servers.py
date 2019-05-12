#!/usr/bin/env python3

""" Launch and organize servers for the python tests. """

import logging
import os
import shutil
import subprocess
import threading
import queue

import psutil


# ### Private constants
_KIVALOO_TEST_DIR = "/tmp/kivaloo-test/"
# - this is relative to TEST_DIR
# - this may be subjected to rm -rf
_DISK_LBS = "lbs-disk"

# These filenames are relative to this directory
_BIN_LBS = "../../lbs/lbs"
_BIN_KVLDS = "../../kvlds/kvlds"

# ### Public constants
LBS_BLOCKSIZE = 512

LBS_SOCK = os.path.join(_KIVALOO_TEST_DIR, "kivaloo-lbs-sock")
KVLDS_SOCK = os.path.join(_KIVALOO_TEST_DIR, "kivaloo-kvlds-sock")


def _enqueue_output(output, queue_toadd):
    """ Reads data from a file descriptor and queues it.  Usually launched
        in a separate thread to provide non-blocking output.
    """
    for line in iter(output.readline, b''):
        queue_toadd.put(line)
    output.close()


class StartError(RuntimeError):
    """ We failed to start a server. """
    pass


class Server(object):
    """ Base class for interacting with a server. """
    # Constants (will be set by subclasses)
    cmd = []
    pidfile = None
    sock = None
    # Variables
    proc = None

    def __init__(self):
        if not os.path.exists(_KIVALOO_TEST_DIR):
            os.mkdir(_KIVALOO_TEST_DIR)

        # Variables to support non-blocking stderr from the server
        self.stderr_queue = queue.Queue()
        self.stderr_thread = None

    def _start(self):
        """ Start the server, or find an existing server.  Should be called
            automatically by the subclass.
        """

        # cls refers to the derived class.  Concretely, this gives us:
        # - one cls.proc shared between all Server_lbs objects
        # - one cls.proc shared between all Server_kvlds objects
        cls = type(self)

        if cls.proc:
            logging.info("Server %s, pid %i: exists; reusing", self.cmd[0],
                         self.get_pid_from_file())
            return
        proc_unowned = self._search_for_process()
        if proc_unowned:
            logging.info("Terminating old process: %s", proc_unowned)
            proc_unowned.terminate()
            if os.path.exists(cls.pidfile):
                os.remove(cls.pidfile)

        # Clean up previous files
        if self.sock and os.path.exists(self.sock):
            os.remove(self.sock)

        # Initialize server and start gathering stderr
        cls.proc = subprocess.Popen(self.cmd, stderr=subprocess.PIPE)
        self.stderr_thread = threading.Thread(target=_enqueue_output,
                                              args=(cls.proc.stderr,
                                                    self.stderr_queue))
        self.stderr_thread.start()

        # Check for server fail
        ret = cls.proc.wait()
        if ret is not 0:
            msg = "Error when running:\n%s\n\texitcode: %i, stderr:\n%s" % (
                " ".join(self.cmd), ret, self.get_stderr())
            # We don't have a running server
            cls.proc = None
            raise StartError(msg)

        # Get server's daemon-forked pid
        logging.info("Server %s, pid %i: started", self.cmd[0],
                     self.get_pid_from_file())

    def get_stderr(self):
        """ Get stderr from the server.  Does not block. """
        if self.stderr_queue.qsize():
            stderr = ""
            while self.stderr_queue.qsize():
                stderr += self.stderr_queue.get_nowait().decode()
        else:
            stderr = None
        return stderr

    @classmethod
    def get_pid_from_file(cls):
        """ Get the server's daemon-forked pid. """
        if not os.path.exists(cls.pidfile):
            return None
        with open(cls.pidfile) as filep:
            return int(filep.read())

    @classmethod
    def _search_for_process(cls):
        """ Try to find an existing server process. """
        # Check existing pidfile
        pid = cls.get_pid_from_file()
        if pid:
            proc = psutil.Process(pid)
            if proc.cmdline() == cls.cmd:
                return proc

        # Look for the process
        for proc in psutil.process_iter():
            if proc.cmdline() == cls.cmd:
                return proc
        return None

    @classmethod
    def shutdown(cls):
        """ Shut down the server. """
        # The pid of self.proc is the pre-forked server's pid, so we get the
        # pid of the daemonized server.
        proc_unowned = cls._search_for_process()
        if proc_unowned is not None:
            proc_unowned.terminate()
            ret = proc_unowned.wait()
            # Did the server exit correctly?
            if ret is not None and ret != 0:
                raise Exception("Failed to shut down properly.")
            logging.info("Server %s exited", cls.cmd[0])
            if os.path.exists(cls.pidfile):
                os.remove(cls.pidfile)

        # One way or another, the previous server is unusable.
        cls.proc = None


class Server_lbs(Server):
    """ Interact with an lbs server. """
    # Constant for Server_lbs
    disk = os.path.join(_KIVALOO_TEST_DIR, _DISK_LBS)
    # Constants for Server
    sock = LBS_SOCK
    cmd = ("%s -s %s -d %s -b %i" % (_BIN_LBS, sock, disk,
                                     LBS_BLOCKSIZE)).split()
    pidfile = sock + ".pid"
    # Variable shared between all Server_lbs objects
    proc = None

    def __init__(self):
        super().__init__()
        if not os.path.exists(self.disk):
            os.mkdir(self.disk)

        self._start()

    def reset(self):
        """ Delete the lbs data and start the server again. """
        self.shutdown()
        shutil.rmtree(self.disk)
        os.mkdir(self.disk)
        self._start()


class Server_kvlds(Server):
    """ Interact with a kvlds server. """
    # Constant for Server_kvlds
    sock_lbs = LBS_SOCK
    # Constants for Server
    sock = os.path.join(_KIVALOO_TEST_DIR, KVLDS_SOCK)
    cmd = ("%s -s %s -l %s" % (_BIN_KVLDS, sock, sock_lbs)).split()
    pidfile = sock + ".pid"
    # Variable shared between all Server_kvlds objects
    proc = None

    def __init__(self):
        super().__init__()
        self._start()

import grp
import os
import os.path
import psutil
import glob
import pwd
import shutil
import signal
import socket
import sys
import tempfile
import json
import stat
from robot.libraries.BuiltIn import BuiltIn
from robot.api import logger

if sys.version_info > (3,):
    long = int
try:
    from urllib.request import urlopen
except:
    from urllib2 import urlopen
try:
    import http.client as httplib
except:
    import httplib

def Check_JSON(j):
    d = json.loads(j, strict=True)
    logger.debug('got json %s' % d)
    assert len(d) > 0
    assert 'error' not in d
    return d

def cleanup_temporary_directory(directory):
    shutil.rmtree(directory)

def save_run_results(directory, filenames):
    current_directory = os.getcwd()
    suite_name = BuiltIn().get_variable_value("${SUITE_NAME}")
    test_name = BuiltIn().get_variable_value("${TEST NAME}")
    onlyfiles = [f for f in os.listdir(directory) if os.path.isfile(os.path.join(directory, f))]
    logger.debug('%s content before cleanup: %s' % (directory, onlyfiles))
    if test_name is None:
        # this is suite-level tear down
        destination_directory = "%s/robot-save/%s" % (current_directory, suite_name)
    else:
        destination_directory = "%s/robot-save/%s/%s" % (current_directory, suite_name, test_name)
    if not os.path.isdir(destination_directory):
        os.makedirs(destination_directory)
    for file in filenames.split(' '):
        source_file = "%s/%s" % (directory, file)
        logger.debug('check if we can save %s' % source_file)
        if os.path.isfile(source_file):
            logger.debug('found %s, save it' % file)
            shutil.copy(source_file, "%s/%s" % (destination_directory, file))
            shutil.copy(source_file, "%s/robot-save/%s.last" % (current_directory, file))

def encode_filename(filename):
    return "".join(['%%%0X' % ord(b) for b in filename])

def get_test_directory():
    return os.path.abspath(os.path.dirname(os.path.realpath(__file__)) + "../../")

def get_top_dir():
    if os.environ.get('RSPAMD_TOPDIR'):
        return os.environ['RSPAMD_TOPDIR']

    return get_test_directory() + "/../../"

def get_install_root():
    if os.environ.get('RSPAMD_INSTALLROOT'):
        return os.path.abspath(os.environ['RSPAMD_INSTALLROOT'])

    return os.path.abspath("../install/")

def get_rspamd():
    if os.environ.get('RSPAMD'):
        return os.environ['RSPAMD']
    if os.environ.get('RSPAMD_INSTALLROOT'):
        return os.environ['RSPAMD_INSTALLROOT'] + "/bin/rspamd"
    dname = get_top_dir()
    return dname + "/src/rspamd"

def get_rspamc():
    if os.environ.get('RSPAMC'):
        return os.environ['RSPAMC']
    if os.environ.get('RSPAMD_INSTALLROOT'):
        return os.environ['RSPAMD_INSTALLROOT'] + "/bin/rspamc"
    dname = get_top_dir()
    return dname + "/src/client/rspamc"

def get_rspamadm():
    if os.environ.get('RSPAMADM'):
        return os.environ['RSPAMADM']
    if os.environ.get('RSPAMD_INSTALLROOT'):
        return os.environ['RSPAMD_INSTALLROOT'] + "/bin/rspamadm"
    dname = get_top_dir()
    return dname + "/src/rspamadm/rspamadm"

def HTTP(method, host, port, path, data=None, headers={}):
    c = httplib.HTTPConnection("%s:%s" % (host, port))
    c.request(method, path, data, headers)
    r = c.getresponse()
    t = r.read()
    s = r.status
    c.close()
    return [s, t]

def make_temporary_directory():
    """Creates and returns a unique temporary directory

    Example:
    | ${TMPDIR} = | Make Temporary Directory |
    """
    dirname = tempfile.mkdtemp()
    os.chmod(dirname, stat.S_IRUSR |
             stat.S_IXUSR |
             stat.S_IWUSR |
             stat.S_IRGRP |
             stat.S_IXGRP |
             stat.S_IROTH |
             stat.S_IXOTH)
    return dirname

def make_temporary_file():
    return tempfile.mktemp()

def path_splitter(path):
    dirname = os.path.dirname(path)
    basename = os.path.basename(path)
    return [dirname, basename]

def read_log_from_position(filename, offset):
    offset = long(offset)
    with open(filename, 'rb') as f:
        f.seek(offset)
        goo = f.read()
        size = len(goo)
    return [goo, size+offset]

def rspamc(addr, port, filename):
    mboxgoo = b"From MAILER-DAEMON Fri May 13 19:17:40 2016\r\n"
    goo = open(filename, 'rb').read()
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((addr, port))
    s.send(b"CHECK RSPAMC/1.0\r\nContent-length: ")
    s.send(str(len(goo+mboxgoo)).encode('utf-8'))
    s.send(b"\r\n\r\n")
    s.send(mboxgoo)
    s.send(goo)
    r = s.recv(2048)
    return r.decode('utf-8')

def scan_file(addr, port, filename):
    return str(urlopen("http://%s:%s/symbols?file=%s" % (addr, port, filename)).read())

def Send_SIGUSR1(pid):
    pid = int(pid)
    os.kill(pid, signal.SIGUSR1)

def set_directory_ownership(path, username, groupname):
    if os.getuid() == 0:
        uid=pwd.getpwnam(username).pw_uid
        gid=grp.getgrnam(groupname).gr_gid
        os.chown(path, uid, gid)

def spamc(addr, port, filename):
    goo = open(filename, 'rb').read()
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((addr, port))
    s.send(b"SYMBOLS SPAMC/1.0\r\nContent-length: ")
    s.send(str(len(goo)).encode('utf-8'))
    s.send(b"\r\n\r\n")
    s.send(goo)
    s.shutdown(socket.SHUT_WR)
    r = s.recv(2048)
    return r.decode('utf-8')

def TCP_Connect(addr, port):
    """Attempts to open a TCP connection to specified address:port

    Example:
    | Wait Until Keyword Succeeds | 5s | 10ms | TCP Connect | localhost | 8080 |
    """
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(5) # seconds
    s.connect((addr, port))
    s.close()

def ping_rspamd(addr, port):
    return str(urlopen("http://%s:%s/ping" % (addr, port)).read())

def redis_check(addr, port):
    """Attempts to open a TCP connection to specified address:port

    Example:
    | Wait Until Keyword Succeeds | 5s | 10ms | TCP Connect | localhost | 8080 |
    """
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(1.0) # seconds
    s.connect((addr, port))
    if s.sendall(b"ECHO TEST\n"):
        result = s.recv(128)
        return result == b'TEST\n'
    else:
        return False

def update_dictionary(a, b):
    a.update(b)
    return a


TERM_TIMEOUT = 10  # wait after sending a SIGTERM signal
KILL_WAIT = 20  # additional wait after sending a SIGKILL signal

def shutdown_process(process):
    # send SIGTERM
    process.terminate()

    try:
        process.wait(TERM_TIMEOUT)
        return
    except psutil.TimeoutExpired:
        logger.info( "PID {} is not terminated in {} seconds, sending SIGKILL...".format(process.pid, TERM_TIMEOUT))
        try:
            # send SIGKILL
            process.kill()
        except psutil.NoSuchProcess:
            # process exited just before we tried to kill
            return

    try:
        process.wait(KILL_WAIT)
    except psutil.TimeoutExpired:
        raise RuntimeError("Failed to shutdown process {} ({})".format(process.pid, process.name()))


def shutdown_process_with_children(pid):
    pid = int(pid)
    try:
        process = psutil.Process(pid=pid)
    except psutil.NoSuchProcess:
        return
    children = process.children(recursive=False)
    shutdown_process(process)
    for child in children:
        try:
            shutdown_process(child)
        except:
            pass

def write_to_stdin(process_handle, text):
    lib = BuiltIn().get_library_instance('Process')
    obj = lib.get_process_object()
    obj.stdin.write(text + "\n")
    obj.stdin.flush()
    obj.stdin.close()
    out = obj.stdout.read(4096)
    return out

def get_file_if_exists(file_path):
    if os.path.exists(file_path):
        with open(file_path, 'r') as myfile:
            return myfile.read()
    return None

# copy-paste from
# https://hg.python.org/cpython/file/6860263c05b3/Lib/shutil.py#l1068
# As soon as we move to Python 3, this should be removed in favor of shutil.which()
def python3_which(cmd, mode=os.F_OK | os.X_OK, path=None):
    """Given a command, mode, and a PATH string, return the path which
    conforms to the given mode on the PATH, or None if there is no such
    file.

    `mode` defaults to os.F_OK | os.X_OK. `path` defaults to the result
    of os.environ.get("PATH"), or can be overridden with a custom search
    path.
    """

    # Check that a given file can be accessed with the correct mode.
    # Additionally check that `file` is not a directory, as on Windows
    # directories pass the os.access check.
    def _access_check(fn, mode):
        return (os.path.exists(fn) and os.access(fn, mode)
                and not os.path.isdir(fn))

    # If we're given a path with a directory part, look it up directly rather
    # than referring to PATH directories. This includes checking relative to the
    # current directory, e.g. ./script
    if os.path.dirname(cmd):
        if _access_check(cmd, mode):
            return cmd
        return None

    if path is None:
        path = os.environ.get("PATH", os.defpath)
    if not path:
        return None
    path = path.split(os.pathsep)

    if sys.platform == "win32":
        # The current directory takes precedence on Windows.
        if not os.curdir in path:
            path.insert(0, os.curdir)

        # PATHEXT is necessary to check on Windows.
        pathext = os.environ.get("PATHEXT", "").split(os.pathsep)
        # See if the given file matches any of the expected path extensions.
        # This will allow us to short circuit when given "python.exe".
        # If it does match, only test that one, otherwise we have to try
        # others.
        if any(cmd.lower().endswith(ext.lower()) for ext in pathext):
            files = [cmd]
        else:
            files = [cmd + ext for ext in pathext]
    else:
        # On other platforms you don't have things like PATHEXT to tell you
        # what file suffixes are executable, so just pass on cmd as-is.
        files = [cmd]

    seen = set()
    for dir in path:
        normdir = os.path.normcase(dir)
        if not normdir in seen:
            seen.add(normdir)
            for thefile in files:
                name = os.path.join(dir, thefile)
                if _access_check(name, mode):
                    return name
    return None


def _merge_luacov_stats(statsfile, coverage):
    """
    Reads a coverage stats file written by luacov and merges coverage data to
    'coverage' dict: { src_file: hits_list }

    Format of the file defined in:
    https://github.com/keplerproject/luacov/blob/master/src/luacov/stats.lua
    """
    with open(statsfile, 'rb') as fh:
        while True:
            # max_line:filename
            line = fh.readline().rstrip()
            if not line:
                break

            max_line, src_file = line.split(':')
            counts = [int(x) for x in fh.readline().split()]
            assert len(counts) == int(max_line)

            if src_file in coverage:
                # enlarge list if needed: lenght of list in different luacov.stats.out files may differ
                old_len = len(coverage[src_file])
                new_len = len(counts)
                if new_len > old_len:
                    coverage[src_file].extend([0] * (new_len - old_len))
                # sum execution counts for each line
                for l, exe_cnt in enumerate(counts):
                    coverage[src_file][l] += exe_cnt
            else:
                coverage[src_file] = counts


def _dump_luacov_stats(statsfile, coverage):
    """
    Saves data to the luacov stats file. Existing file is overwritted if exists.
    """
    src_files = sorted(coverage)

    with open(statsfile, 'wb') as fh:
        for src in src_files:
            stats = " ".join(str(n) for n in coverage[src])
            fh.write("%s:%s\n%s\n" % (len(coverage[src]), src, stats))


# File used by luacov to collect coverage stats
LUA_STATSFILE = "luacov.stats.out"


def collect_lua_coverage():
    """
    Merges ${TMPDIR}/*.luacov.stats.out into luacov.stats.out

    Example:
    | Collect Lua Coverage |
    """
    # decided not to do optional coverage so far
    #if not 'ENABLE_LUA_COVERAGE' in os.environ['HOME']:
    #    logger.info("ENABLE_LUA_COVERAGE is not present in env, will not collect Lua coverage")
    #    return

    tmp_dir = BuiltIn().get_variable_value("${TMPDIR}")

    coverage = {}
    input_files = []

    for f in glob.iglob("%s/*.luacov.stats.out" % tmp_dir):
        _merge_luacov_stats(f, coverage)
        input_files.append(f)

    if input_files:
        if os.path.isfile(LUA_STATSFILE):
            _merge_luacov_stats(LUA_STATSFILE, coverage)
        _dump_luacov_stats(LUA_STATSFILE, coverage)
        logger.info("%s merged into %s" % (", ".join(input_files), LUA_STATSFILE))
    else:
        logger.info("no *.luacov.stats.out files found in %s" % tmp_dir)

#!/usr/bin/env python3
import socket
import time
import os
import subprocess
import signal
import sys

HOST = "127.0.0.1"
PORT = 18888
CONFIG = "/tmp/test_server.conf"
LOGFILE = "/tmp/test_server.log"
DBFILE = "chat.db"

def make_config():
    with open(CONFIG, "w") as f:
        f.write(f"""
port = {PORT}
thread_pool_size = 4
pool_min_threads = 1
pool_shrink_idle_sec = 5
max_clients = 5
max_rooms = 3
max_members = 3
history_size = 3
rate_bucket_max = 5
rate_refill_rate = 1.0
rate_msg_cost = 1
tls_cert = server.crt
tls_key = server.key
log_level = DEBUG
log_file = {LOGFILE}
""")
    # remove old log and db
    for p in [LOGFILE, DBFILE]:
        try: os.remove(p)
        except: pass
    # also remove via wsl path
    try: os.remove("/mnt/d/Multi-Client-Chat-Server-with-Presence/chat.db")
    except: pass
    try: os.remove("/mnt/d/Multi-Client-Chat-Server-with-Presence/chat.db-wal")
    except: pass
    try: os.remove("/mnt/d/Multi-Client-Chat-Server-with-Presence/chat.db-shm")
    except: pass
    try: os.remove("/mnt/d/Multi-Client-Chat-Server-with-Presence/server.log")
    except: pass

class Client:
    def __init__(self, host=HOST, port=PORT):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(5)
        self.sock.connect((host, port))
        # read greeting byte
        greeting = self.sock.recv(1)
        # print(f"Greeting: {greeting}")
        self.buf = b""
    def send(self, cmd):
        self.sock.sendall((cmd + "\n").encode())
    def recv_line(self, timeout=2):
        self.sock.settimeout(timeout)
        while b"\n" not in self.buf:
            try:
                data = self.sock.recv(4096)
                if not data:
                    return None
                self.buf += data
            except socket.timeout:
                return None
        line, self.buf = self.buf.split(b"\n", 1)
        return line.decode().strip()
    def recv_all(self, timeout=0.5):
        # collect all available lines within timeout
        lines = []
        self.sock.settimeout(timeout)
        start = time.time()
        while time.time() - start < timeout:
            try:
                # try to read
                if b"\n" in self.buf:
                    line, self.buf = self.buf.split(b"\n", 1)
                    lines.append(line.decode().strip())
                    continue
                data = self.sock.recv(4096)
                if not data:
                    break
                self.buf += data
            except socket.timeout:
                break
        # flush any remaining buf lines
        while b"\n" in self.buf:
            line, self.buf = self.buf.split(b"\n", 1)
            lines.append(line.decode().strip())
        return lines
    def close(self):
        try: self.sock.close()
        except: pass

def test_register_and_session():
    print("=== test_register_and_session ===")
    c = Client()
    c.send("REGISTER alice secret123")
    line = c.recv_line()
    print(f"REGISTER reply: {line}")
    assert line and line.startswith("OK REGISTERED"), f"expected OK REGISTERED, got {line}"
    parts = line.split()
    assert len(parts) >= 2, "no token?"
    token = parts[2] if len(parts)>=3 else None
    assert token and len(token)==64, f"invalid token {token}"
    print(f"Token: {token}")
    c.close()
    return token

def test_login_and_reconnect():
    print("=== test_login_and_reconnect ===")
    # first ensure user exists, then login new connection
    c = Client()
    c.send("LOGIN alice secret123")
    line = c.recv_line()
    print(f"LOGIN reply: {line}")
    assert line and "LOGGED_IN" in line, f"login failed {line}"
    parts = line.split()
    token = parts[2] if len(parts)>=3 else None
    print(f"Login token: {token}")
    c.close()
    time.sleep(0.2)
    # reconnect with valid token
    c2 = Client()
    c2.send(f"RECONNECT {token}")
    line = c2.recv_line()
    print(f"RECONNECT valid reply: {line}")
    assert line and "RECONNECTED" in line, f"reconnect failed {line}"
    parts = line.split()
    new_token = parts[2] if len(parts)>=3 else None
    assert new_token and len(new_token)==64 and new_token != token, "token rotation failed"
    print(f"New token: {new_token}")
    # old token should be invalid now
    c3 = Client()
    c3.send(f"RECONNECT {token}")
    line = c3.recv_line()
    print(f"RECONNECT old token reply: {line}")
    assert line and ("INVALID_TOKEN" in line or "SESSION_EXPIRED" in line or "ERR" in line), f"old token should be invalid {line}"
    c3.close()
    # invalid token
    c4 = Client()
    c4.send("RECONNECT invalidtoken123")
    line = c4.recv_line()
    print(f"RECONNECT invalid reply: {line}")
    assert "INVALID_TOKEN" in line or "ERR" in line
    c4.close()
    # keep c2 for later tests, but close for now
    c2.close()
    return new_token

def test_dynamic_rooms():
    print("=== test_dynamic_rooms ===")
    # clean start: ensure no leftover rooms by using fresh server state is not possible without restart, so just test with 2 rooms
    # use a single client to create one room, then leave and create another to verify non-leak
    c = Client()
    c.send("LOGIN alice secret123")
    line = c.recv_line()
    assert "LOGGED_IN" in line
    c.send("CREATE roomA")
    line = c.recv_line()
    print(f"CREATE roomA: {line}")
    assert "CREATED" in line
    c.send("LEAVE")
    line = c.recv_line()
    print(f"LEAVE roomA: {line}")
    c.send("CREATE roomB")
    line = c.recv_line()
    print(f"CREATE roomB: {line}")
    assert "CREATED" in line
    c.close()
    time.sleep(0.2)
    # now test max_rooms limit with multiple clients (config max_rooms=3, we have roomB still? After alice left and disconnected, roomB should be deleted because she left then disconnected, but we left then created roomB and then disconnected without leaving, so roomB will be deleted on disconnect)
    # Wait, after LEAVE, roomA deleted, then CREATE roomB, then close without LEAVE -> roomB will be deleted on disconnect, so rooms empty
    # Now test 3 rooms creation
    clients = []
    for i in range(1,4):
        cl = Client()
        cl.send(f"REGISTER user_room{i} pass{i}")
        l = cl.recv_line()
        print(f"REGISTER user_room{i}: {l}")
        # Need to ensure username not already exists from previous run - use unique names with timestamp
        cl.send(f"CREATE roomX{i}")
        l = cl.recv_line()
        print(f"CREATE roomX{i}: {l}")
        assert "CREATED" in l, f"expected CREATED for roomX{i}, got {l}"
        clients.append(cl)
    # 4th room should fail (max_rooms 3)
    cl = Client()
    cl.send("REGISTER user_room4 pass4")
    l = cl.recv_line()
    print(f"REGISTER user_room4: {l}")
    cl.send("CREATE roomX4")
    line = cl.recv_line()
    print(f"CREATE roomX4 (should fail max rooms): {line}")
    assert "MAX_ROOM_COUNT_REACHED" in line or "ERR" in line, f"expected max rooms error, got {line}"
    cl.send("ROOMS")
    lines = cl.recv_all(0.5)
    print(f"ROOMS reply: {lines}")
    assert any("ROOMS_REPLY" in l for l in lines)
    cl.close()
    for cl in clients:
        cl.close()
    time.sleep(0.3)

def test_pagination():
    print("=== test_pagination ===")
    c = Client()
    c.send("LOGIN alice secret123")
    l = c.recv_line()
    print(f"LOGIN alice: {l}")
    # Use helper clients to keep 3 rooms alive for pagination (max_rooms=3)
    keepers = []
    for i in range(1,4):
        k = Client()
        # Use unique names to avoid DB conflict if re-run; append timestamp
        import time as _time
        uname = f"keep{i}_{int(_time.time())%10000}"
        # But keep simple for now
        k.send(f"REGISTER keepPag{i} pass{i}")
        rl = k.recv_line()
        print(f"REGISTER keepPag{i}: {rl}")
        # If username taken, try login
        if "USERNAME_TAKEN" in rl:
            k.close()
            k = Client()
            k.send(f"LOGIN keepPag{i} pass{i}")
            print(f"LOGIN keepPag{i}: {k.recv_line()}")
        k.send(f"CREATE keepRoom{i}")
        print(f"keepRoom{i}: {k.recv_line()}")
        keepers.append(k)
    time.sleep(0.2)
    c.send("ROOMS")
    line = c.recv_line()
    print(f"ROOMS (no pagination): {line}")
    assert "ROOMS_REPLY" in line
    # Should have at least 3 rooms
    c.send("ROOMS 0 1")
    line = c.recv_line()
    print(f"ROOMS 0 1: {line}")
    parts = line.split()
    assert parts[0]=="ROOMS_REPLY"
    if len(parts) >=4:
        total = int(parts[1]); offset=int(parts[2]); count=int(parts[3])
        print(f"Pagination ROOMS: total={total} offset={offset} count={count}")
        assert total>=3, f"expected total>=3 got {total}"
        assert offset==0
        assert count==1
    c.send("ROOMS 1 1")
    line = c.recv_line()
    print(f"ROOMS 1 1: {line}")
    c.send("ROOMS 10 5")
    line = c.recv_line()
    print(f"ROOMS 10 5 (beyond): {line}")
    parts = line.split()
    if len(parts)>=4:
        assert int(parts[3])==0 # count 0 when beyond
    c.send("ROOMS 0 1000")
    line = c.recv_line()
    print(f"ROOMS 0 1000: {line}")
    # Test WHO pagination: use existing keepRoom1 which has keeper
    c.send("JOIN keepRoom1")
    line = c.recv_line()
    print(f"JOIN keepRoom1 for WHO test: {line}")
    assert "JOINED" in line
    # drain history
    c.recv_all(0.5)
    # Add another user to same room
    c2 = Client()
    c2.send("REGISTER bobPag2 pass2")
    rl = c2.recv_line()
    print(f"REGISTER bobPag2: {rl}")
    if "USERNAME_TAKEN" in rl:
        c2.close()
        c2 = Client()
        c2.send("LOGIN bobPag2 pass2")
        print(f"LOGIN bobPag2: {c2.recv_line()}")
    c2.send("JOIN keepRoom1")
    line2 = c2.recv_line()
    print(f"bob JOIN keepRoom1: {line2}")
    # drain history
    c2.recv_all(0.5)
    c.recv_all(0.5)
    # WHO without pagination
    c.send("WHO")
    line = c.recv_line()
    print(f"WHO: {line}")
    assert "WHO_REPLY" in line
    c.send("WHO 0 1")
    line = c.recv_line()
    print(f"WHO 0 1: {line}")
    parts = line.split()
    assert parts[0]=="WHO_REPLY"
    if len(parts)>=4:
        total=int(parts[1]); offset=int(parts[2]); count=int(parts[3])
        assert count==1
        assert total>=2
    c.send("WHO 1 1")
    line = c.recv_line()
    print(f"WHO 1 1: {line}")
    c.send("WHO 10 5")
    line = c.recv_line()
    print(f"WHO 10 5: {line}")
    if "WHO_REPLY" in line:
        parts=line.split()
        if len(parts)>=4:
            assert int(parts[3])==0
    c2.close()
    c.close()
    for k in keepers:
        k.close()
    time.sleep(0.3)
    print("Pagination tests passed")

def test_history_and_presence():
    print("=== test_history_and_presence ===")
    c = Client()
    c.send("LOGIN alice secret123")
    print(f"LOGIN hist: {c.recv_line()}")
    c.send("CREATE histRoom")
    print(f"CREATE histRoom: {c.recv_line()}")
    for i in range(5):
        c.send(f"MSG hello {i}")
        line = c.recv_line()
        print(f"MSG {i}: {line}")
        assert "SENT" in line or "OK" in line
    # Keep c alive, have c2 join to test history replay (in-memory)
    c2 = Client()
    c2.send("REGISTER histUser pass")
    rl = c2.recv_line()
    print(f"REGISTER histUser: {rl}")
    if "USERNAME_TAKEN" in rl:
        c2.close()
        c2 = Client()
        c2.send("LOGIN histUser pass")
        print(f"LOGIN histUser: {c2.recv_line()}")
    c2.send("JOIN histRoom")
    # Need to handle JOIN reply plus history: first line is OK JOINED, then history
    line = c2.recv_line()
    print(f"JOIN histRoom first: {line}")
    assert "JOINED" in line or "OK" in line
    lines = c2.recv_all(1)
    print(f"JOIN histRoom history lines: {lines}")
    # also need to include first line
    all_lines = [line] + lines
    has_history = any("history" in l for l in all_lines)
    has_msg = any("MSG" in l for l in all_lines)
    print(f"has_history={has_history}, has_msg={has_msg}")
    assert has_history, "should receive history"
    assert has_msg, "should receive MSG history"
    # Count MSG history lines (excluding headers)
    msg_count = sum(1 for l in all_lines if l.startswith("MSG "))
    print(f"History msg_count={msg_count} (expected <=3)")
    assert msg_count <= 3
    c2.close()
    c.close()
    time.sleep(0.3)
    # Test persistence after restart: recreate histRoom and check DB history
    c3 = Client()
    c3.send("LOGIN histUser pass")
    print(f"LOGIN histUser2: {c3.recv_line()}")
    c3.send("JOIN histRoom")
    # This will fail if room was deleted (since both left). Try CREATE instead if JOIN fails
    line = c3.recv_line()
    print(f"JOIN histRoom after restart: {line}")
    if "ROOM_NOT_FOUND" in line:
        c3.send("CREATE histRoom")
        line = c3.recv_line()
        print(f"CREATE histRoom after delete: {line}")
        # Check history after recreate: need another client to join to see history
        c4 = Client()
        c4.send("REGISTER histUser2 pass2")
        rl = c4.recv_line()
        print(f"REGISTER histUser2: {rl}")
        c4.send("JOIN histRoom")
        l = c4.recv_line()
        print(f"JOIN histRoom2: {l}")
        lines = c4.recv_all(1)
        print(f"History after recreate: {lines}")
        c4.close()
    c3.close()

    # presence test
    c3 = Client()
    c3.send("LOGIN alice secret123")
    print(f"LOGIN alice presence: {c3.recv_line()}")
    c3.send("STATUS AWAY")
    line = c3.recv_line()
    print(f"STATUS AWAY: {line}")
    assert "STATUS_SET" in line or "OK" in line
    c3.close()
    time.sleep(0.2)

def test_logging():
    print("=== test_logging ===")
    # Check log file exists and contains expected levels
    time.sleep(0.5)
    try:
        with open("/tmp/test_server.log","r", encoding="utf-8", errors="ignore") as f:
            content = f.read()
            print(f"Log file size {len(content)}")
            print(content[:1000])
            assert "[INFO]" in content or "[DEBUG]" in content
            assert "Server listening" in content or "Registry" in content
            # Ensure no passwords or tokens logged
            assert "secret123" not in content
            assert "pass" not in content.lower() or "password" not in content.lower()
            print("Logging test passed")
    except FileNotFoundError:
        print("Log file not found, checking server.log in cwd")
        try:
            with open("server.log","r", encoding="utf-8", errors="ignore") as f:
                print(f.read()[:500])
        except: print("no log file")

def test_rate_limit():
    print("=== test_rate_limit ===")
    c = Client()
    c.send("LOGIN alice secret123")
    c.recv_line()
    c.send("CREATE rateRoom")
    c.recv_line()
    # flood msgs quickly
    for i in range(10):
        c.send(f"MSG flood {i}")
    lines = c.recv_all(1)
    print(f"Rate limit lines: {lines}")
    has_rate = any("RATE_LIMITED" in l for l in lines)
    print(f"Rate limited triggered: {has_rate}")
    # Might be rate limited after 5 msgs
    c.close()

if __name__ == "__main__":
    make_config()
    # start server
    print(f"Starting server with config {CONFIG} on port {PORT}")
    proc = subprocess.Popen(["./server", CONFIG], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    time.sleep(1.5)
    if proc.poll() is not None:
        out, err = proc.communicate()
        print("Server failed to start")
        print(out.decode())
        print(err.decode())
        sys.exit(1)
    try:
        token = test_register_and_session()
        test_login_and_reconnect()
        test_dynamic_rooms()
        test_pagination()
        test_history_and_presence()
        test_rate_limit()
        test_logging()
        print("\n=== ALL INTEGRATION TESTS PASSED ===")
    except AssertionError as e:
        print(f"\nTEST FAILED: {e}")
        import traceback; traceback.print_exc()
        sys.exit(1)
    except Exception as e:
        print(f"\nTEST ERROR: {e}")
        import traceback; traceback.print_exc()
        sys.exit(1)
    finally:
        print("Shutting down server")
        proc.send_signal(signal.SIGINT)
        try: proc.wait(timeout=3)
        except: proc.kill()
        time.sleep(0.5)
        # cleanup
        try: os.remove(CONFIG)
        except: pass

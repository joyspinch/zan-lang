import socket, subprocess, time, sys, urllib.request, json, os

PROJ = r"d:\project\zan-lang\templates\server\server-iot"
EXE = os.path.join(PROJ, "_scratch_iot.exe")

def enc_str(s):
    b = s.encode()
    return len(b).to_bytes(2, "big") + b

def enc_remlen(n):
    out = b""
    while True:
        d = n % 128
        n //= 128
        if n > 0:
            d |= 128
        out += bytes([d])
        if n == 0:
            break
    return out

def connect(cid):
    vh = enc_str("MQTT") + bytes([4, 0x02]) + (60).to_bytes(2, "big")
    rem = vh + enc_str(cid)
    return bytes([0x10]) + enc_remlen(len(rem)) + rem

def subscribe(pid, flt):
    rem = pid.to_bytes(2, "big") + enc_str(flt) + bytes([0])
    return bytes([0x82]) + enc_remlen(len(rem)) + rem

def publish(topic, payload):
    rem = enc_str(topic) + payload.encode()
    return bytes([0x30]) + enc_remlen(len(rem)) + rem

def http(path, data=None, token=None):
    url = "http://127.0.0.1:8080" + path
    hdr = {}
    body = None
    if data is not None:
        body = data.encode()
        hdr["Content-Type"] = "application/x-www-form-urlencoded"
    if token:
        hdr["Authorization"] = "Bearer " + token
    req = urllib.request.Request(url, data=body, headers=hdr,
                                 method="POST" if data is not None else "GET")
    with urllib.request.urlopen(req, timeout=5) as r:
        return r.status, r.read().decode("utf-8", "replace")

results = []
def check(name, cond, extra=""):
    results.append((name, cond, extra))
    print(("PASS" if cond else "FAIL"), name, extra)

srv = subprocess.Popen([EXE], cwd=PROJ,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
time.sleep(2.5)
try:
    # 1. HTTP dashboard
    st, body = http("/")
    check("GET / dashboard 200", st == 200 and "IoT Console" in body)

    # 2. MQTT subscriber
    sub = socket.create_connection(("127.0.0.1", 1883), timeout=5)
    sub.sendall(connect("subber"))
    ack = sub.recv(16)
    check("sub CONNACK", ack[0] == 0x20 and ack[3] == 0)
    sub.sendall(subscribe(1, "sensors/#"))
    sack = sub.recv(16)
    check("SUBACK", sack[0] == 0x90)

    # 3. MQTT publisher
    pub = socket.create_connection(("127.0.0.1", 1883), timeout=5)
    pub.sendall(connect("pubber"))
    pub.recv(16)
    pub.sendall(publish("sensors/room1/temp", "22.5"))

    # 4. subscriber receives the routed PUBLISH
    sub.settimeout(3)
    data = sub.recv(64)
    got = (data[0] & 0xF0) == 0x30 and b"sensors/room1/temp" in data and b"22.5" in data
    check("subscriber received PUBLISH", got, repr(data))

    # 5. management API reflects state
    st, c = http("/iot/clients")
    check("GET /iot/clients has 2", c.count('"client_id"') == 2, c)
    st, s = http("/iot/subscriptions")
    check("GET /iot/subscriptions has filter", "sensors/#" in s, s)
    st, t = http("/iot/topics")
    check("GET /iot/topics has topic", "sensors/room1/temp" in t, t)
    st, m = http("/iot/metrics")
    md = json.loads(m)["data"]
    check("metrics messages_in>=1", md["messages_in"] >= 1, m)
    check("metrics messages_out>=1", md["messages_out"] >= 1)
    check("metrics clients_connected==2", md["clients_connected"] == 2)

    # 6. auth-gated publish via HTTP API
    st, lg = http("/auth/login", "user=admin&pass=admin")
    tok = json.loads(lg)["data"]["token"]
    check("login issues token", len(tok) > 0)
    st, pr = http("/iot/publish", "topic=sensors/room1/hum&payload=55", token=tok)
    pd = json.loads(pr)["data"]
    check("HTTP publish delivered==1", pd["delivered"] == 1, pr)
    # subscriber (sensors/#) gets it
    data2 = sub.recv(64)
    check("subscriber received HTTP-injected PUBLISH",
          b"sensors/room1/hum" in data2 and b"55" in data2, repr(data2))

    # 7. unauthenticated publish rejected
    try:
        http("/iot/publish", "topic=x&payload=y")
        check("unauth publish 401", False)
    except urllib.error.HTTPError as e:
        check("unauth publish 401", e.code == 401, str(e.code))

    sub.close(); pub.close()
finally:
    srv.terminate()
    try:
        out = srv.stdout.read().decode("utf-8", "replace")
    except Exception:
        out = ""

print("\n----- SERVER LOG (tail) -----")
print("\n".join(out.splitlines()[:25]))
npass = sum(1 for _, c, _ in results if c)
print("\nSUMMARY: %d/%d passed" % (npass, len(results)))
sys.exit(0 if npass == len(results) else 1)

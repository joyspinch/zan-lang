#!/usr/bin/env python3
"""
纯 Zan DAP 服务端协议与实机 GDB 集成测试（B-ID201）。

覆盖两部分：
1. 纯协议会话测试（不依赖 gdb）：
   - initialize 能力声明 + initialized 事件
   - setBreakpoints（普通/条件/hitCondition 含 ==K、>=K、非法 %K 拒绝/logMessage）
   - setExceptionBreakpoints（throw + unhandled）
   - attach 缺 processId 失败应答
   - pause 在非运行态失败应答
   - zan/heapSnapshot 空闲态摘要
   - disconnect 触发 terminated + exited 事件并干净退出
2. 实机 GDB 端到端集成测试（传入 --integration-exe 与目标程序时）：
   - 直接以纯 Zan DAP 二进制作为被测适配器调用 dap_integration_test.exe，
     验证 run_basic / run_hitcount / run_logpoint / run_pause 全套 4 组实机用例。
"""
import argparse
import json
import os
import subprocess
import sys


def encode_frame(obj):
    raw = json.dumps(obj, separators=(",", ":")).encode("utf-8")
    return f"Content-Length: {len(raw)}\r\n\r\n".encode("ascii") + raw


def read_frame(stream):
    content_len = None
    while True:
        line = stream.readline()
        if not line:
            return None
        s = line.decode("ascii", errors="replace").strip()
        if s == "":
            if content_len is not None:
                break
            continue
        if s.lower().startswith("content-length:"):
            content_len = int(s.split(":", 1)[1].strip())
    if not content_len or content_len <= 0:
        return None
    body = stream.read(content_len)
    if len(body) != content_len:
        return None
    return json.loads(body.decode("utf-8"))


def run_protocol_tests(dap_exe):
    proc = subprocess.Popen(
        [dap_exe],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    seq = 0

    def send(cmd, args=None):
        nonlocal seq
        seq += 1
        req = {"seq": seq, "type": "request", "command": cmd}
        if args is not None:
            req["arguments"] = args
        proc.stdin.write(encode_frame(req))
        proc.stdin.flush()
        return seq

    def recv_until_response(req_seq):
        events = []
        while True:
            msg = read_frame(proc.stdout)
            if msg is None:
                err = proc.stderr.read().decode("utf-8", errors="replace")
                raise AssertionError(f"EOF waiting response seq={req_seq}; stderr={err}")
            if msg.get("type") == "response" and msg.get("request_seq") == req_seq:
                return msg, events
            events.append(msg)

    # 1. initialize + initialized
    s = send("initialize", {"adapterID": "zan", "linesStartAt1": True})
    resp, _ = recv_until_response(s)
    assert resp["success"], f"initialize failed: {resp}"
    body = resp["body"]
    for cap in (
        "supportsConfigurationDoneRequest",
        "supportsConditionalBreakpoints",
        "supportsHitConditionalBreakpoints",
        "supportsLogPoints",
        "supportsEvaluateForHovers",
        "supportsSetVariable",
        "supportsTerminateRequest",
        "supportsExceptionInfoRequest",
        "supportsExceptionFilterOptions",
    ):
        assert body.get(cap) is True, f"missing capability {cap}: {body}"
    ev = read_frame(proc.stdout)
    assert ev and ev.get("type") == "event" and ev.get("event") == "initialized", f"expected initialized event, got {ev}"

    # 2. setBreakpoints (normal, conditional, hitCondition valid/invalid, logMessage)
    s = send("setBreakpoints", {
        "source": {"path": "sample.zan"},
        "breakpoints": [
            {"line": 10},
            {"line": 12, "condition": "x > 3"},
            {"line": 14, "hitCondition": "==5"},
            {"line": 15, "hitCondition": ">=2"},
            {"line": 16, "hitCondition": "%2"},
            {"line": 18, "logMessage": "hit x={x}"},
        ],
    })
    resp, _ = recv_until_response(s)
    assert resp["success"], f"setBreakpoints failed: {resp}"
    bps = resp["body"]["breakpoints"]
    assert len(bps) == 6, f"expected 6 breakpoints, got {bps}"
    assert bps[0]["verified"] is True and bps[0]["line"] == 10
    assert bps[1]["verified"] is True and bps[1].get("message") == "x > 3"
    assert bps[2]["verified"] is True and bps[2]["line"] == 14
    assert bps[3]["verified"] is True and bps[3]["line"] == 15
    assert bps[4]["verified"] is False and "unsupported hitCondition" in bps[4].get("message", "")
    assert bps[5]["verified"] is True and bps[5]["line"] == 18

    # 3. setExceptionBreakpoints
    s = send("setExceptionBreakpoints", {"filters": ["throw", "unhandled"]})
    resp, _ = recv_until_response(s)
    assert resp["success"], f"setExceptionBreakpoints failed: {resp}"

    # 4. attach without processId -> failure
    s = send("attach", {"program": "demo.exe"})
    resp, _ = recv_until_response(s)
    assert resp["success"] is False, f"attach without pid should fail: {resp}"

    # 5. pause while idle -> failure
    s = send("pause", {"threadId": 1})
    resp, _ = recv_until_response(s)
    assert resp["success"] is False, f"pause while idle should fail: {resp}"

    # 6. zan/heapSnapshot while idle
    s = send("zan/heapSnapshot", {})
    resp, _ = recv_until_response(s)
    assert resp["success"] is True, f"heapSnapshot failed: {resp}"
    assert resp["body"]["summary"]["state"] == "idle", f"unexpected state: {resp}"

    # 7. scopes / threads / stackTrace while idle
    s = send("scopes", {"frameId": 0})
    resp, _ = recv_until_response(s)
    assert resp["success"] and resp["body"]["scopes"][0]["variablesReference"] == 1000

    s = send("threads")
    resp, _ = recv_until_response(s)
    assert resp["success"] and len(resp["body"]["threads"]) >= 1

    # 8. disconnect -> terminated + exited + clean exit
    s = send("disconnect")
    resp, evs = recv_until_response(s)
    assert resp["success"], f"disconnect failed: {resp}"
    saw_terminated = any(e.get("event") == "terminated" for e in evs)
    saw_exited = any(e.get("event") == "exited" for e in evs)
    while not (saw_terminated and saw_exited):
        m = read_frame(proc.stdout)
        if m is None:
            break
        if m.get("type") == "event":
            if m.get("event") == "terminated":
                saw_terminated = True
            elif m.get("event") == "exited":
                saw_exited = True
    assert saw_terminated and saw_exited, f"expected terminated+exited events, got terminated={saw_terminated} exited={saw_exited}"
    rc = proc.wait(timeout=10)
    assert rc == 0, f"dap server exited with rc={rc}"
    print("[PASS] DAP protocol unit suite (8/8 checks)")


def run_gdb_integration(dap_exe, integration_exe, target_exe, target_src, burn_exe, burn_src):
    cmd = [integration_exe, dap_exe, target_exe, target_src, burn_exe, burn_src]
    res = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    if res.stdout:
        print(res.stdout, end="")
    if res.stderr:
        print(res.stderr, file=sys.stderr, end="")
    if res.returncode == 77:
        print("[SKIP] DAP live-GDB integration suite skipped (exit 77, no gdb / ZAN_ALLOW_SKIP_DAP=1)")
        return False
    if res.returncode != 0:
        raise AssertionError(f"dap_integration_test failed with rc={res.returncode}")
    print("[PASS] DAP live-GDB integration suite (run_basic, run_hitcount, run_logpoint, run_pause)")
    return True


def run_gdb_extended_parity(dap_exe, target_exe, target_src):
    """实机 GDB 进阶契约验证：条件断点、setVariable、watch 作用域、threads/selectThread、stepIn/stepOut、exceptionInfo。"""
    proc = subprocess.Popen(
        [dap_exe],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    seq = 0

    def send(cmd, args=None):
        nonlocal seq
        seq += 1
        req = {"seq": seq, "type": "request", "command": cmd}
        if args is not None:
            req["arguments"] = args
        proc.stdin.write(encode_frame(req))
        proc.stdin.flush()
        return seq

    def recv_resp(req_seq):
        evs = []
        while True:
            m = read_frame(proc.stdout)
            if m is None:
                err = proc.stderr.read().decode("utf-8", errors="replace")
                raise AssertionError(f"EOF waiting seq={req_seq}; stderr={err}")
            if m.get("type") == "response" and m.get("request_seq") == req_seq:
                return m, evs
            evs.append(m)

    def wait_event(name):
        while True:
            m = read_frame(proc.stdout)
            if m is None:
                raise AssertionError(f"EOF waiting event {name}")
            if m.get("type") == "event" and m.get("event") == name:
                return m

    s = send("initialize", {"adapterID": "zan", "linesStartAt1": True})
    resp, _ = recv_resp(s)
    assert resp["success"]
    wait_event("initialized")

    # Line 16: `int s = Accumulate(n);` (stepIn target)
    # Line 8: `acc = acc + i;` with condition `i == 3`
    s = send("setBreakpoints", {
        "source": {"path": target_src},
        "breakpoints": [
            {"line": 16},
            {"line": 8, "condition": "i == 3"},
        ],
    })
    resp, _ = recv_resp(s)
    assert resp["success"] and len(resp["body"]["breakpoints"]) == 2

    s = send("launch", {"program": target_exe})
    resp, _ = recv_resp(s)
    assert resp["success"]

    s = send("configurationDone")
    resp, _ = recv_resp(s)
    assert resp["success"]
    ev = wait_event("stopped")
    assert ev["body"]["reason"] == "breakpoint", f"expected breakpoint stop, got {ev}"

    # 1. stepIn from Main line 16 into Accumulate
    s = send("stepIn", {"threadId": 1})
    resp, _ = recv_resp(s)
    assert resp["success"]
    ev = wait_event("stopped")
    assert ev["body"]["reason"] in ("step", "breakpoint"), f"unexpected stepIn stop: {ev}"
    s = send("stackTrace", {"threadId": 1})
    resp, _ = recv_resp(s)
    frames = resp["body"]["stackFrames"]
    assert any("Accumulate" in f.get("name", "") for f in frames), f"expected Accumulate frame after stepIn: {frames}"

    # 2. continue to conditional breakpoint i == 3 (acc should be 0+1+2 = 3)
    s = send("continue", {"threadId": 1})
    resp, _ = recv_resp(s)
    assert resp["success"]
    ev = wait_event("stopped")
    assert ev["body"]["reason"] == "breakpoint", f"expected conditional breakpoint stop, got {ev}"

    s = send("evaluate", {"expression": "i"})
    resp, _ = recv_resp(s)
    assert resp["success"] and resp["body"]["result"] == "3", f"expected i==3, got {resp}"

    s = send("evaluate", {"expression": "acc"})
    resp, _ = recv_resp(s)
    assert resp["success"] and resp["body"]["result"] == "3", f"expected acc==3, got {resp}"

    # 3. setVariable mutates acc -> 100
    s = send("setVariable", {"variablesReference": 1000, "name": "acc", "value": "100"})
    resp, _ = recv_resp(s)
    assert resp["success"], f"setVariable failed: {resp}"
    s = send("evaluate", {"expression": "acc"})
    resp, _ = recv_resp(s)
    assert resp["success"] and resp["body"]["result"] == "100", f"expected mutated acc==100, got {resp}"

    # 4. watch context -> Watch scope (2000) -> variables(2000) == 103
    s = send("evaluate", {"expression": "acc + i", "context": "watch"})
    resp, _ = recv_resp(s)
    assert resp["success"] and resp["body"]["result"] == "103", f"watch eval failed: {resp}"

    s = send("scopes", {"frameId": 0})
    resp, _ = recv_resp(s)
    scopes = resp["body"]["scopes"]
    assert any(sc["name"] == "Watch" and sc["variablesReference"] == 2000 for sc in scopes), f"missing Watch scope: {scopes}"

    s = send("variables", {"variablesReference": 2000})
    resp, _ = recv_resp(s)
    wvars = resp["body"]["variables"]
    assert len(wvars) == 1 and wvars[0]["name"] == "acc + i" and wvars[0]["value"] == "103", f"unexpected watch vars: {wvars}"

    # 5. threads + selectThread + exceptionInfo
    s = send("threads")
    resp, _ = recv_resp(s)
    tid = resp["body"]["threads"][0]["id"]
    s = send("selectThread", {"threadId": tid})
    resp, _ = recv_resp(s)
    assert resp["success"]

    s = send("exceptionInfo", {"threadId": tid})
    resp, _ = recv_resp(s)
    assert resp["success"] and "exceptionId" in resp["body"]

    # 6. stepOut back to Main and finish cleanly
    s = send("stepOut", {"threadId": tid})
    resp, _ = recv_resp(s)
    assert resp["success"]
    wait_event("stopped")
    s = send("stackTrace", {"threadId": tid})
    resp, _ = recv_resp(s)
    top_name = resp["body"]["stackFrames"][0]["name"]
    assert "Main" in top_name or "main" in top_name, f"expected Main frame after stepOut, got {resp['body']['stackFrames']}"

    s = send("continue", {"threadId": tid})
    resp, _ = recv_resp(s)
    assert resp["success"]
    wait_event("exited")

    s = send("disconnect")
    resp, _ = recv_resp(s)
    assert resp["success"]
    rc = proc.wait(timeout=10)
    assert rc == 0, f"dap server exited with rc={rc}"
    print("[PASS] DAP live-GDB extended parity suite (conditional bp, setVariable, watch scope, stepIn/stepOut, threads/selectThread, exceptionInfo)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True, help="Path to pure-Zan DAP executable")
    ap.add_argument("--integration-exe", default="", help="Optional path to dap_integration_test.exe")
    ap.add_argument("--target-exe", default="")
    ap.add_argument("--target-src", default="")
    ap.add_argument("--burn-exe", default="")
    ap.add_argument("--burn-src", default="")
    args = ap.parse_args()

    run_protocol_tests(args.exe)
    if args.integration_exe and os.path.exists(args.integration_exe):
        ran_gdb = run_gdb_integration(
            args.exe,
            args.integration_exe,
            args.target_exe,
            args.target_src,
            args.burn_exe,
            args.burn_src,
        )
        if ran_gdb and args.target_exe and os.path.exists(args.target_exe):
            run_gdb_extended_parity(args.exe, args.target_exe, args.target_src)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""On-device UI stress battery for grumpyscreen alpha.163 (runs ON pono-pi).

Jack's queue: flawless UI -> STRESS TEST -> OMEGA. This is the stress test.
Drives Moonraker state churn against every consume()/overlay/prompt hot path
that the .161-.163 fixes touched, plus link-drop recovery, and watches for
crash/respawn (PID), leak (RSS), stranded overlays, and wedges.

No motion, no homing, no extrusion, no heat (target churn only, never held).
Captures fb0 as .b64 into /tmp for off-box decode (PIL lives on the laptop).

PASS criteria logged per phase; summary at the end.
"""
import json
import subprocess
import threading
import time
import urllib.parse
import urllib.request

H = "http://192.168.50.112"
SSH = ["ssh", "-T", "-o", "ConnectTimeout=20", "-o", "StrictHostKeyChecking=no",
       "root@192.168.50.112"]
RESULTS = []


def log(s):
    print(s, flush=True)


def phase(name, ok, detail=""):
    RESULTS.append((name, ok, detail))
    log(f"[{'PASS' if ok else 'FAIL'}] {name} {detail}")


def gcode(g, wait=True, timeout=40):
    url = H + "/printer/gcode/script?script=" + urllib.parse.quote(g)

    def run():
        try:
            urllib.request.urlopen(url, timeout=timeout)
        except Exception:
            pass  # dwell-blocked responses are expected

    if wait:
        run()
    else:
        threading.Thread(target=run, daemon=True).start()


def post(path):
    try:
        req = urllib.request.Request(H + path, data=b"", method="POST")
        with urllib.request.urlopen(req, timeout=15) as r:
            return r.status
    except Exception as e:
        return f"ERR:{e}"


def query():
    try:
        u = (H + "/printer/objects/query?print_stats&idle_timeout"
             "&display_status&extruder&heater_bed&fan")
        with urllib.request.urlopen(u, timeout=8) as r:
            return json.load(r)["result"]["status"]
    except Exception as e:
        return {"_err": str(e)}


def cap(name):
    cmd = ("python3 -c \"import base64,sys;"
           "sys.stdout.write(base64.b64encode(open('/dev/fb0','rb').read()).decode())\"")
    r = subprocess.run(SSH + [cmd], capture_output=True, text=True, timeout=35)
    b = r.stdout.strip()
    if len(b) > 1000:
        with open(f"/tmp/stress_{name}.b64", "w") as f:
            f.write(b)
        log(f"  [cap] stress_{name}.b64 ok")
        return True
    log(f"  [cap] {name} EMPTY stderr={r.stderr[:120]}")
    return False


def pid_rss():
    r = subprocess.run(
        SSH + ["P=$(pidof grumpyscreen); echo PID=$P; grep VmRSS /proc/$P/status"],
        capture_output=True, text=True, timeout=25)
    out = r.stdout.replace("\n", " ").strip()
    pid = rss = None
    for tok in out.split():
        if tok.startswith("PID="):
            pid = tok[4:]
        if tok.isdigit():
            rss = int(tok)  # VmRSS kB (last bare number wins)
    return pid, rss, out


def wait_klippy_ready(deadline_s=60):
    t0 = time.time()
    while time.time() - t0 < deadline_s:
        try:
            with urllib.request.urlopen(H + "/printer/info", timeout=5) as r:
                if json.load(r)["result"]["state"] == "ready":
                    return True
        except Exception:
            pass
        time.sleep(2)
    return False


log("=== grumpyscreen alpha.163 UI stress battery ===")

# Phase 0: baseline ---------------------------------------------------------
st = query()
ps = st.get("print_stats", {}).get("state")
pid0, rss0, raw0 = pid_rss()
log(f"baseline: print_stats={ps} idle={st.get('idle_timeout', {}).get('state')} "
    f"msg={st.get('display_status', {}).get('message')!r} {raw0}")
cap("00_baseline")
if ps != "standby" or not pid0:
    phase("0 baseline", False, f"state={ps} pid={pid0} - aborting")
    raise SystemExit(1)
phase("0 baseline", True, f"pid={pid0} rss={rss0}kB")
gcode("PONO_CAL_SKIP")  # dismiss any pending post-update prompt (harmless if none)
time.sleep(2)

# Phase 1: cal-overlay churn (show/update/hide x12) --------------------------
try:
    for i in range(1, 13):
        gcode(f'SET_DISPLAY_TEXT MSG="Calibrating stress {i}/12: overlay churn"\nG4 P900')
        if i == 6:
            cap("01_overlay_mid")
    gcode('SET_DISPLAY_TEXT MSG=""')
    time.sleep(2)
    p, _, _ = pid_rss()
    phase("1 overlay churn x12", p == pid0, f"pid={p}")
except Exception as e:
    phase("1 overlay churn x12", False, str(e))

# Phase 2: prompt over overlay, open/close x6 -------------------------------
try:
    for i in range(1, 7):
        seq = "\n".join([
            f'SET_DISPLAY_TEXT MSG="Calibrating stress prompt {i}/6"',
            f'RESPOND TYPE=command MSG="action:prompt_begin Stress {i}/6"',
            'RESPOND TYPE=command MSG="action:prompt_text Prompt churn over the cal overlay."',
            'RESPOND TYPE=command MSG="action:prompt_footer_button OK|_CLOSE_PROMPT|primary"',
            'RESPOND TYPE=command MSG="action:prompt_show"',
            'G4 P1500',
            'RESPOND TYPE=command MSG="action:prompt_end"',
            'G4 P400',
        ])
        if i == 3:
            gcode(seq, wait=False)
            time.sleep(1.2)
            cap("02_prompt_mid")
            time.sleep(2.0)
        else:
            gcode(seq)
    gcode('SET_DISPLAY_TEXT MSG=""')
    time.sleep(2)
    p, _, _ = pid_rss()
    phase("2 prompt churn x6", p == pid0, f"pid={p}")
except Exception as e:
    phase("2 prompt churn x6", False, str(e))

# Phase 3: print-state flips via dwell job x4 + cancel ----------------------
try:
    dwell = ('SET_DISPLAY_TEXT MSG="Calibrating OMEGA 0/29: stress dwell"\n'
             'G4 P6000\nM117\n')
    with open("/tmp/stress_dwell.gcode", "w") as f:
        f.write(dwell)
    r = subprocess.run(
        ["curl", "-s", "-o", "/dev/null", "-w", "%{http_code}",
         "-F", "file=@/tmp/stress_dwell.gcode", H + "/server/files/upload"],
        capture_output=True, text=True, timeout=20)
    log(f"  upload: {r.stdout}")
    flips_ok = True
    for i in range(1, 5):
        sc = post("/printer/print/start?filename=stress_dwell.gcode")
        time.sleep(3)
        st = query()
        mid_state = st.get("print_stats", {}).get("state")
        mid_msg = st.get("display_status", {}).get("message")
        if i == 1:
            cap("03_print_mid")  # banner observation evidence (deferred bug)
            log(f"  gate inputs during print: state={mid_state} msg={mid_msg!r}")
        if mid_state != "printing":
            flips_ok = False
            log(f"  flip {i}: state={mid_state} (start rc={sc})")
        # wait for job end
        t0 = time.time()
        while time.time() - t0 < 25:
            if query().get("print_stats", {}).get("state") in ("complete", "standby", "cancelled"):
                break
            time.sleep(1.5)
    # cancel path: start then cancel mid-dwell
    post("/printer/print/start?filename=stress_dwell.gcode")
    time.sleep(2.5)
    post("/printer/print/cancel")
    time.sleep(3)
    end_state = query().get("print_stats", {}).get("state")
    p, _, _ = pid_rss()
    phase("3 print flips x4 + cancel", flips_ok and p == pid0 and end_state in ("cancelled", "standby"),
          f"pid={p} end={end_state}")
    cap("03_post_flips")
except Exception as e:
    phase("3 print flips x4 + cancel", False, str(e))

# Phase 4a: FIRMWARE_RESTART mid-print (link-drop recovery) ------------------
try:
    post("/printer/print/start?filename=stress_dwell.gcode")
    time.sleep(2.5)
    post("/printer/firmware_restart")
    ok = wait_klippy_ready(75)
    time.sleep(4)
    st = query()
    p, _, _ = pid_rss()
    cap("04_post_fwrestart")
    phase("4a firmware_restart mid-print", ok and p == pid0,
          f"ready={ok} pid={p} state={st.get('print_stats', {}).get('state')} "
          f"msg={st.get('display_status', {}).get('message')!r}")
except Exception as e:
    phase("4a firmware_restart mid-print", False, str(e))

# Phase 4b: moonraker restart (ws drop -> reset_overlay_state) ---------------
try:
    gcode('SET_DISPLAY_TEXT MSG="Calibrating stress: ws-drop"\nG4 P6000', wait=False)
    time.sleep(1.5)
    subprocess.run(SSH + ["/etc/init.d/moonraker restart"], capture_output=True,
                   text=True, timeout=30)
    time.sleep(10)
    ok = wait_klippy_ready(60)
    time.sleep(5)
    st = query()
    p, _, _ = pid_rss()
    cap("04_post_moonrestart")
    phase("4b moonraker restart mid-overlay", ok and p == pid0,
          f"ready={ok} pid={p} msg={st.get('display_status', {}).get('message')!r}")
except Exception as e:
    phase("4b moonraker restart mid-overlay", False, str(e))

# Phase 5: temp-target + fan churn ------------------------------------------
try:
    for i in range(6):
        gcode("M104 S46" if i % 2 == 0 else "M104 S0")
        time.sleep(1.2)
    gcode("M104 S0")
    for s in (77, 178, 255, 0, 128, 0, 255, 77, 0):
        gcode(f"M106 S{s}")
        time.sleep(1.0)
    gcode("M107")
    time.sleep(2)
    st = query()
    tgt = st.get("extruder", {}).get("target")
    fan = st.get("fan", {}).get("speed")
    p, _, _ = pid_rss()
    phase("5 temp/fan churn", p == pid0 and tgt == 0 and fan == 0,
          f"pid={p} tgt={tgt} fan={fan}")
except Exception as e:
    phase("5 temp/fan churn", False, str(e))

# Phase 6: final state + leak check -----------------------------------------
gcode('SET_DISPLAY_TEXT MSG=""')
gcode("M117")
time.sleep(2)
st = query()
pidN, rssN, rawN = pid_rss()
cap("06_final")
msg = st.get("display_status", {}).get("message")
ps = st.get("print_stats", {}).get("state")
growth = (rssN - rss0) if (rssN and rss0) else -1
phase("6 final clean state", pidN == pid0 and ps in ("standby", "cancelled", "complete") and not msg,
      f"pid {pid0}->{pidN} rss {rss0}->{rssN}kB (+{growth}kB) state={ps} msg={msg!r}")

# cleanup the uploaded dwell file
try:
    req = urllib.request.Request(H + "/server/files/gcodes/stress_dwell.gcode",
                                 method="DELETE")
    urllib.request.urlopen(req, timeout=10)
    log("  cleanup: stress_dwell.gcode deleted")
except Exception as e:
    log(f"  cleanup skipped: {e}")

log("=== SUMMARY ===")
fails = [r for r in RESULTS if not r[1]]
for name, ok, detail in RESULTS:
    log(f"  {'PASS' if ok else 'FAIL'}  {name}  {detail}")
log(f"=== {len(RESULTS) - len(fails)}/{len(RESULTS)} phases passed ===")
raise SystemExit(0 if not fails else 2)

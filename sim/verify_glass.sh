#!/usr/bin/env bash
# Direct-LAN glass verify for the printer (prime is down). Taps + framebuffer
# captures via the printer's own python3 (no base64 applet, no scp/sftp).
set -u
P=root@192.168.50.112
SSH="ssh -T -o ConnectTimeout=12 -o StrictHostKeyChecking=no"

alive() { for i in $(seq 1 6); do $SSH $P true 2>/dev/null && return 0; sleep 8; done; return 1; }

tap() { # x y
  alive || { echo "tap: printer unreachable"; return 1; }
  printf 'import struct,time\nfd=open("/dev/input/event0","wb",0)\ndef ev(t,c,v): fd.write(struct.pack("IIHHi",0,0,t,c,v))\nx,y=%d,%d\nev(3,0x2f,0);ev(3,0x39,1);ev(3,0x35,x);ev(3,0x36,y)\nev(1,0x14a,1);ev(3,0,x);ev(3,1,y);ev(0,0,0)\ntime.sleep(0.06)\nev(3,0x2f,0);ev(3,0x39,-1);ev(1,0x14a,0);ev(0,0,0)\n' "$1" "$2" | $SSH $P "python3 -" 2>/dev/null
  echo "tapped $1,$2"
}

cap() { # name
  alive || { echo "cap: printer unreachable"; return 1; }
  cat fbcap.py | $SSH $P "python3 -" 2>/dev/null > "dev_$1.b64"
  python -c "
import base64
d=open('dev_$1.b64').read().strip(); raw=base64.b64decode(d) if d else b''
if len(raw)>=524288:
    from PIL import Image
    Image.frombytes('RGBA',(480,272),raw,'raw','BGRA',1920,1).convert('RGB').save('dev_$1.png'); print('  saved dev_$1.png ('+str(len(raw))+')')
else: print('  cap $1 SHORT:', len(raw))
"
}

echo '== skip the firmware-updated popup =='; tap 430 198; sleep 3
echo '== capture idle cockpit =='; cap idle; sleep 2
echo '== open Files =='; tap 240 238; sleep 3
echo '== capture Files =='; cap files; sleep 2
echo '== back to home =='; tap 20 22; sleep 2
echo '== open Move =='; tap 45 238; sleep 3
echo '== capture Move =='; cap move
echo DONE

import base64, sys
sys.stdout.write(base64.b64encode(open('/dev/fb0', 'rb').read()).decode())

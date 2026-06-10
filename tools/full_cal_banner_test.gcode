; Full Cal banner glass-verify: announce a campaign step, then dwell 60s so
; the banner + progress bar can be captured (fb0) with the print state live.
; No motion, no heat. Clear the message afterwards with:
;   SET_DISPLAY_TEXT MSG=""
; (a trailing clear here would execute ahead of the dwell and wipe the test:
; Klipper processes gcode lines ahead of motion-queue time).
SET_DISPLAY_TEXT MSG="Full Cal 5/40: Retract speed"
G4 P60000

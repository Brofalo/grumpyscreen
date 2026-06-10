; omega_banner_test.gcode - glass-verify the OMEGA print banner on-device.
; Sets the OMEGA cal message FIRST (before the dwell, so it executes instead of
; queuing behind it), then a dwell-only virtual_sdcard "print" so
; print_stats.state reads "printing" -- the gate for the OMEGA banner in
; main_panel consume() (printing && display_status.message starts "Calibrating OMEGA").
; No heat, no motion. During the dwell we capture the framebuffer.
SET_DISPLAY_TEXT MSG="Calibrating OMEGA 8/29: Bridging (7 OK)"
G4 P45000

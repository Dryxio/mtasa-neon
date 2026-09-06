# FPS counter

Copy `fps-counter` into `mods/deathmatch/resources/`, then run `refresh` and
`start fps-counter` in the server console. Each player can type `/fps` to
show or hide a local counter in the bottom-right corner.

The counter starts hidden and averages frame timing over 500 ms. It does not
change the frame limiter or send measurements to the server. Stopping the
resource removes the display.

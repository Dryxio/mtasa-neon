# Custom window layouts

Neon supports an opt-in windowed resolution and borderless placement per client
profile. This is useful for multiple clients, capture layouts and multi-monitor
desktops. Ordinary profiles keep the existing behavior.

Close the clients before editing `MTA/config/coreconfig.xml` (primary) and
`MTA/config/coreconfig-cl2.xml` (the client launched with `-cl2`). Add or update
these elements inside `<mainconfig><settings>`; do not add duplicate elements:

```xml
<display_windowed>1</display_windowed>
<display_windowed_width>960</display_windowed_width>
<display_windowed_height>1080</display_windowed_height>
<display_windowed_borderless>1</display_windowed_borderless>
<display_windowed_position>1</display_windowed_position>
<display_windowed_x>0</display_windowed_x>
<display_windowed_y>0</display_windowed_y>
<aspect_ratio>0</aspect_ratio>
```

For two clients covering a 1920 x 1080 desktop, use the same settings in both
profiles, with `display_windowed_x` set to `960` for the secondary client.
Use automatic aspect ratio (`0`) to match the actual render dimensions. Hide
the Windows taskbar for an unobstructed capture. Launch the primary normally
and the secondary with `-cl2`, or use the updated MTA Neon Duo launcher.

All layout changes require a client restart. Coordinates are the outer window's
top-left desktop position; negative coordinates can address another monitor.
Positions are clamped to the nearest monitor to keep windows reachable.

Width and height must both be set, between 640 x 480 and 16384 x 16384, within
the device's capabilities. Existing desktop-size safety checks still apply;
prefer dimensions no larger than the target desktop. The custom mode uses
32-bit color and is added to the game's mode list so camera and render dimensions
agree. Unsupported requests can fall back to an ordinary video mode. Portrait
layouts may expose assumptions in resource-specific HUDs; these must be checked
in the resources being recorded.

The options are configured through XML in this prototype, with no additional
settings-panel controls. While custom dimensions are enabled, they take
precedence over the resolution selected in the settings panel on the next start.
Set both dimensions to `0` to resume normal resolution selection. Set
`display_windowed_borderless` and `display_windowed_position` to `0` to restore
framed, automatically placed windows. Fullscreen mode ignores these options.

The Duo launcher preserves valid custom dimensions, and its placement helper
leaves native borderless layouts in place. Existing framed profiles retain the
legacy 800 x 600 arrangement.

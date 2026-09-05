-- Minimal configuration for the nested hyprcast development session.

hl.monitor({
    output = "",
    mode = "1280x720@60",
    position = "auto",
    scale = 1,
})

local mainMod = "ALT"
local terminal = "ghostty --gtk-single-instance=false"

hl.bind(mainMod .. " + Return", hl.dsp.exec_cmd(terminal))
hl.bind(mainMod .. " + Q", hl.dsp.window.close())
hl.bind(mainMod .. " + M", hl.dsp.exit())

hl.bind(mainMod .. " + H", hl.dsp.focus({ direction = "left" }))
hl.bind(mainMod .. " + J", hl.dsp.focus({ direction = "down" }))
hl.bind(mainMod .. " + K", hl.dsp.focus({ direction = "up" }))
hl.bind(mainMod .. " + L", hl.dsp.focus({ direction = "right" }))

hl.bind(mainMod .. " + mouse:272", hl.dsp.window.drag(), { mouse = true })
hl.bind(mainMod .. " + mouse:273", hl.dsp.window.resize(), { mouse = true })

hl.config({
    general = {
        gaps_in = 4,
        gaps_out = 8,
        border_size = 3,
        col = {
            active_border = "rgba(ff4d4dff)",
            inactive_border = "rgba(663333cc)",
        },
        layout = "dwindle",
    },

    decoration = {
        rounding = 4,
        blur = {
            enabled = false,
        },
        shadow = {
            enabled = false,
        },
    },

    animations = {
        enabled = false,
    },

    input = {
        kb_layout = "us",
        follow_mouse = 1,
    },

    misc = {
        disable_hyprland_logo = true,
        force_default_wallpaper = 0,
    },

    debug = {
        disable_logs = false,
        enable_stdout_logs = true,
    },
})

# WinMame

## Features

**Direct3D & VSync fix**

    Using Direct3D video mode with -waitvsync option doesn't add 2-3 frames of lag anymore.

**Frame delay**

    -frame_delay <0-16> (ms)
    -fd <0-16> (ms)

    Adds 0 to 16 milliseconds of delay between frames to process users input.
    For example, on a modern computer a value of 8 ms gives good results.
    Only available if vsync is enabled.

**Throttle threshold**

    -throttle_threshold <percent_error>
    -tt <percent_error>
    
    Detects if a game is running natively at the same refresh rate of your main monitor.
    If the vsync option is enabled and the refresh rates matches then unthrottled mode
    is activated to have perfect smooth animation.
    
    Example : game X runs natively at 59Hz, your screen refresh rate is 60Hz.
    With a percent_error of 3% the frequencies are considered equals so the game will run
    unthrottled at 60Hz if vsync is enabled. The animation will be perfectly smooth.
    The game will be sightly faster (by a very small margin).

**Skip game info**

    The -skip_gameinfo (-sgi) option now hides the game disclaimer, warning and loading popup message.

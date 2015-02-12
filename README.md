# WinMame

## Features

**Better Direct3D & VSync support**

    Using Direct3D video mode with -waitvsync option doesn't add 2-3 frames of lag anymore.

**Frame delay option**

    A -frame_delay (-fd) option has been added in order to reduce input lag in unthrottled + vsync mode.
    Choose a value between 0-16 ms of additionnal delay between frames to process users input.
    On a modern computer a value of 8 ms gives good results.

**Throttle threshold option**

    -throttle_threshold <percent_error>
    -tt <percent_error>
    
    Detects if a game is running natively at the same refresh rate of your main monitor.
    If the vsync option is enabled and the refresh rates matches then unthrottled mode
    is activated to have perfect smooth animation.
    
    Example : game X runs natively at 59Hz, your screen refresh rate is 60Hz.
    With a percent_error of 3% the frequencies are considered equals so the game will run
    unthrottled at 60Hz if vsync is enabled. The animation will be perfectly smooth.
    The game will be sightly faster (by a very small margin).

**Enhanced skip_gameinfo option**

    The -skip_gameinfo (-sgi) option now hides the game disclaimer, warning and loading popup message.

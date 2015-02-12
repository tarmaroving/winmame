# WinMame

## Features

**Better Direct3D & VSync support**

    Using Direct3D video mode with -waitvsync option doesn't add 2-3 frames of lag anymore.

**Frame delay option**

    A -frame_delay option has been added in order to reduce input lag in unthrottled + vsync mode.
    Choose a value between 0-16 ms of additionnal delay between frames to process users input.
    On a modern computer a value of 8 ms gives good results.

**Throttle threshold option**

    cool

**Enhanced skip_gameinfo option**

    The -skip_gameinfo option now hides the game disclaimer, warning and loading popup message.
    A shorthand has been added under the name -sgi.

# Mini vMac 3DS Port

Macintosh Plus with 4MB variant  
Most changes/3ds GLUE in src/MYOSGLUE.c

# What's working:
Boots on real hardware  
1BPP Framebuffer conversion and display  
Basic input using the DPAD/Circle Pad  
Touchscreen mouse  

# TODO:
Fix logging bug that locks the emulator  
Code cleanup  
Remove unused (all) SDL Audio code  
Scroll screen centered on mouse in scaled modes  

# TODO (At some point):
Sound  
Keyboard input  
User interface  

# Using
Place vMac.ROM in /3ds/vmac/ along with your disk images  
Disks must be autoloaded at the moment so name them disk1.dsk, disk2.dsk, ect...
  
L and R Shoulder buttons are a mouse click  
CPad Is not working correctly as mouse input (yet)  
Touchscreen input works for moving the mouse  
START Exits (Be sure to do Special->Shutdown first)  
SELECT Toggles screen scaling mode  

# Notes
This is a very early WIP port so if it crashes/hangs/murders you... I dunno...  
The bottom screen is used for debug info and also says hi.  
The scaled modes do not yet follow the mouse so the unscaled mode is useless.  

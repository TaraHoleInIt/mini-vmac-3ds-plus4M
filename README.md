# Mini vMac 3DS Port

Macintosh Plus with 4MB variant  
Most changes/3ds GLUE in src/MYOSGLUE.c

# What's working:
It boots on hardware!  
Touchscreen mouse working  
CPad mouse working (hack)  
Basic on screen keyboard  

# TODO:
Code cleanup  
Remove unused (all) SDL Audio code   

# TODO (At some point):
Sound  

# Using
Place vMac.ROM in /3ds/vmac/ along with your disk images  
Place ui_kb_lc.png and ui_kb_uc.png in /3ds/vmac/gfx  
Disks must be autoloaded at the moment so name them disk1.dsk, disk2.dsk, ect...  
  
L and R Shoulder buttons are a mouse click    
L + R + START Exits (Be sure to do Special->Shutdown first)  
SELECT Toggles screen scaling mode  
START Toggles the Mini vMac control mode interface  
Y Toggles the on screen keyboard  

# Notes
This is a very early WIP port so if it crashes/hangs/murders you... I dunno...  
Not all keys are implemented in keyboard mode.  
There is no visual feedback for pressed keys. (yet)  



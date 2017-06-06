## Updated version: https://github.com/TaraHoleInIt/minivmac-3ds
## GBATemp thread: http://gbatemp.net/threads/mini-vmac-for-3ds.439119/

# Mini vMac 3DS Port

Macintosh Plus with 4MB variant  
Most changes/3ds GLUE in src/MYOSGLUE.c

# What's working:
It boots on hardware!  
Touchscreen mouse  
CPad mouse  
Basic on screen keyboard  
DPAD Mapped to arrow keys  

# TODO:
Code cleanup  
Remove unused (all) SDL Audio code   
Optimize/speedup for o3ds  

# TODO (At some point):
Sound  
Support Macintosh II variants  
Support screen widths/heights greater than 512px  

# Using
Place vMac.ROM in /3ds/vmac/ along with your disk images  
Place ui_kb_lc.png, ui_kb_uc.png, and ui_kb_shift.png in /3ds/vmac/gfx  
Disks must be autoloaded at the moment so name them disk1.dsk, disk2.dsk, ect...  
  
L and R Shoulder buttons are a mouse click    
L + R + START Exits (Be sure to do Special->Shutdown first)  
SELECT Toggles screen scaling mode  
START Toggles the Mini vMac control mode interface  
Y Toggles the on screen keyboard  

# Notes
This is a very early WIP port so if it crashes/hangs/murders you... I dunno...  
Some frames may be dropped/skipped when running on an o3ds  

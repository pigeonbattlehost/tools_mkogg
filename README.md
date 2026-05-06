### mkogg

**This project is related to ZenithOS but isn't syncable.**
Creates ogg file by cli.

Example:

```
  mkogg -tone "440 0.5" --wave sine --volume 0.4 -o beep.ogg
```


### Build mkogg

Make sure you have libogg and libvorbis in your host system!

Make the bin:
```
  make
```

Clear everything:
```
  make clean
```

Install:
```
  sudo make install
```

Uninstall:
```
  sudo make uninstall
```

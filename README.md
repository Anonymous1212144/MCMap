# MCMap
A fast program to create a PNG map from region files, with 1 block resolution, using Minecraft map's colour scheme.

## Instructions
Simply drag the executable into the same folder as the region files, and it'll collect and compile them together into a PNG. Some things to note are:
- The output will always be called "output.png" and will overwrite what is already there
- Trying to create a PNG with more than 2 billion pixels may crash the program
- The memory usage should be approximately equal to the number of pixels of the output file (e.g. 1 million pixels would be 1 megabytes)
- The source provided creates a map using the colour scheme of 1.21.5, and is built with that version in mind.
- The program may break if there are invalid region files (e.g. bad file names, 0 bytes, etc.)
- A block's brightness on the map depends on its height difference compared to the block at its north side, but if that area is not loaded, the brightness may be incorrect

## Modification instructions
If you want to apply it to other versions, make sure the `interesting` and `prop_types` in the main cpp are set to that version's equivalent, and remove line 241's `y += 4;` if you intend to run it on a shorter world (e.g. 1.17, end/nether). Also, make sure to edit colours.h to include any new or renamed blocks. The colour codes are a group of two indices to a palette, which you can find in format.h (which is just a template header for PNG), which represents the 2 possible colours the block can take (e.g. fully grown wheat is yellow as opposed to green if it's not). This program requires the zlib library as a dependency and is made on the latest version of C++ currently. Simply get the zlib source and put it in the same folder as these files, and run something like:

`cl /std:c++latest map.cpp adler32.c crc32.c deflate.c inflate.c inftrees.c inffast.c trees.c zutil.c`

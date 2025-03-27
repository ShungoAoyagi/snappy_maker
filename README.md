# Introduction

This program is a tool to compress files in a directory.
It aims for the following features:

- Compress files rapidly and simultaneously with the creation of target files.
- Compress files in a directory with a specific pattern, especially all files with the same prefix.
- Compress files in parallel.
- Can be stopped and resumed.
- Delete the original files after compression.

# Performance

Compress 400 files with 4 MB each takes 3 seconds on my computer.
This is rapid enough to be used in SPring-8.

# Building

```bash
mkdir build && cd build
cmake ..
make
```

In some environments, you may need to execute the following command instead of the above.

```bash
mkdir build && cd build
cmake -G "MinGW Makefiles" -DCMAKE_CXX_COMPILER=g++ ..
make
```

# Usage

Execute build/SnappyMaker.exe to start the program.
In prompt window, you should input the following:

- Directory to watch: the directory to watch for new files.
- Output directory: the directory to output the compressed files.
- Base name: the base name of the files to compress.
- Set size: the number of files to compress at a time.

# Dependencies

We use google/snappy to compress files. To use our program, you should also clone and compile snappy.
https://github.com/google/snappy#

pdb-redo-session-manager
========================

This is the repository containing the code for the PDB-REDO web site and web services.

Before building the software, please make sure you have at least installed a good, modern C++ compiler as well as:

* [cmake](https://cmake.org) Preferrably version 4 or later
* [yarn](https://yarnpkg.org)

Building
--------

Building this tool is as simple as

```bash
yarn
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

Provided of course you've installed all the prerequisites being the aforementioned libzeep, mrc and other requirements as well as a decent C++ compiler (capable of c++-20).

Using
-----

The application usually runs as a daemon process but can run in the foreground.

Use the `--help` option to see all possible commands and options.
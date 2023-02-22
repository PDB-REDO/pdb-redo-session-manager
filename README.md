pdb-redo-session-manager
========================

This is the repository containing the code for the PDB-REDO web site and web services.

This code relies heavily on [libzeep](https://github.com/mhekkel/libzeep). Also, you will have to use [mrc](https://github.com/mhekkel/mrc) to store the web assets as resource. Therefore, if you want to run this on a system that does not use ELF executable format you will have to hack the source.

Other requirements are

* [yarn](https://yarnpkg.org)
* [libpqxx](https://pqxx.org)
* [libmcfp](https://github.com/mhekkel/libmcfp.git)
* [gxrio](https://github.com/mhekkel/gxrio.git)

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
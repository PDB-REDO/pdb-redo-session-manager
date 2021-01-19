pdb-redo-session-manager
========================

This program acts as a session manager for PDB-REDO jobs. There are three major parts in this program, these are

- a REST API for creating, retrieving, updating and deleting sessions and jobs
- a web interface with some sample forms for sending the REST commands
- an admin page for viewing and deleting sessions

The REST API uses authentication similar to e.g. the API of Amazon Web Services. A token must be requested first using username/password. Using this secret token and a public session ID you can then sign API requests and submit them.

Two example libraries are provided using this authentication scheme. One in JavaScript and the other in Perl.

The code itself relies heavily on [libzeep](https://github.com/mhekkel/libzeep). Also, you will have to use [mrc](https://github.com/mhekkel/mrc) to store the web assets as resource. Therefore, if you want to run this on a system that does not use ELF executable format you will have to hack the source and especially the makefile (hint: look at the DEBUG option).

Building
--------

Building this tool is as simple as

```
autoreconf -if
./configure
make
sudo make install
```

Provided of course you've installed all the prerequisites being the aforementioned libzeep and mrc as well as a decent C++ compiler (capable of c++-17) and libboost.
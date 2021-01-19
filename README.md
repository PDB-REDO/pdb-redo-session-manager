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

Using API
---------

The public API for PDB-REDO is REST based. Using the API starts with creating a session. For this you need a valid username/password combination. A session can be created by doing a `POST` request to `https://services.pdb-redo.edu/api/session` with three parameters:

- `user` containing a valid username
- `password` containing the plain text password
- `name` containing a name to be used for this session. This is not really used internally, it is more like a reference for the end user

The result is a JSON object containing:

- `id` the session ID, a number
- `name` the same name as passed in the request
- `token` the secret
- `expires` the date at which the tokens validity expires

The `id` should be stored along with the secret `token`.

Once you have a session ID and secret, you can access the following services:

		// get session info
		GET "https://services.pdb-redo.eu/api/session/{id}"

		// delete a session
		DELETE "https://services.pdb-redo.eu/api/session/{id}"

		// return a list of runs
		GET "https://services.pdb-redo.eu/api/session/{id}/run"

		// Submit a run (job)
		POST "session/{id}/run"
			<- { "mtz-file", "pdb-file", "restraints-file", "sequence-file", "parameters" }

		// return info for a run
		GET "https://services.pdb-redo.eu/api/session/{id}/run/{run}"

		// get a result file
		GET "https://services.pdb-redo.eu/api/session/{id}/run/{run}/output/{file}"

Each request should be signed using the token. The actual implementation will be described later, for now look at the `test-api.js` or `lib/API.pm` files.

The submit call expects a JSON object containing the named parameters containing the contents of the files. The parameters object is currently very limited: it can contain a single boolean name `paired` to indicate the run should do a paired refinement.

Replies are JSON objects, if applicable.
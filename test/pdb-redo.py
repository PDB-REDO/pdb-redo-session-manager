# SPDX-License-Identifier: BSD-2-Clause
# 
# Copyright (c) 2020 NKI/AVL, Netherlands Cancer Institute
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 
# 1. Redistributions of source code must retain the above copyright notice, this
#    list of conditions and the following disclaimer
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import json
from re import sub
from urllib import response
import requests
from PDBRedoAPIAuth import PDBRedoAPIAuth
import argparse
import time
import sys
import getpass

# Due to a bug? in the server implementation, the :port is required here...
PDBREDO_URI = 'https://services.pdb-redo.eu:443'

# This script implements these 'commands':
#
#	login
#	submit
#	status
#	fetch

def usage():
    print("Usage: python pdb-redo.py command")
    print("where command is one of login, submit, status, fetch")
    exit(1)

# Login, retieve a token from the services daemon using username and password

def do_login(args):
    password = args.password
    if (password == None):
        password = getpass.getpass('Password for ' + args.username + ": ")
    
    r = requests.post(args.url + "/api/session", data={"user": args.username, "password": password})
    r.raise_for_status()
    
    payload = r.json()

    print("The token id is:         {0}\n"
          "And the token secret is: {1}\n"
          "And it expires on:       {2}"
          .format(payload.get('id'), payload.get('token'), payload.get('expires')))

def do_submit(args):
    # The token id and secret for a session at PDB-REDO    
    token_id = args.token_id
    token_secret = args.token_secret

    # The authentication object, used by the requests module
    auth = PDBRedoAPIAuth(token_id, token_secret)

    # The files to submit
    xyzin = args.xyzin
    hklin = args.hklin
    paired = args.paired

    files = {
        'pdb-file': open(xyzin, 'rb'),
        'mtz-file': open(hklin, 'rb')
    }
    
    if (args.restraints != None):
        files['restraints-file'] = open(args.restraints, 'rb')
    
    if (args.sequence != None):
        files['sequence-file'] = open(args.sequence, 'rb')

    # Optional parameters, currently there's only one:
    params = {
        'paired': paired
    }
        
    # Create a new job/run
    r = requests.post(args.url + "/api/session/{token_id}/run".format(token_id = token_id), auth = auth, files = files, data = {'parameters': json.dumps(params)})
    r.raise_for_status()

    run_id = r.json()['id']
    print("Job submitted with id", run_id)

def do_status(args):
    # The token id and secret for a session at PDB-REDO    
    token_id = args.token_id
    token_secret = args.token_secret

    # The authentication object, used by the requests module
    auth = PDBRedoAPIAuth(token_id, token_secret)
    
    # The job ID
    run_id = args.job_id

    r = requests.get(args.url + "/api/session/{token_id}/run/{run_id}".format(token_id = token_id, run_id = run_id), auth = auth)
    r.raise_for_status()

    status = r.json()['status']
    
    print("Job status is", status)

def do_fetch(args):
    # The token id and secret for a session at PDB-REDO    
    token_id = args.token_id
    token_secret = args.token_secret

    # The authentication object, used by the requests module
    auth = PDBRedoAPIAuth(token_id, token_secret)
    
    # The job ID
    run_id = args.job_id

    # Retrieve zip, see test-api.py for an example how to retrieve the files seperately
    r = requests.get(args.url + "/api/session/{token_id}/run/{run_id}/output/zipped".format(token_id = token_id, run_id = run_id), auth = auth)
    r.raise_for_status()

    output = args.output
    if (output== None):
        output = '/tmp/result.zip'

    with open(output, 'wb') as f:
        f.write(r.content)

parser = argparse.ArgumentParser(prog='pdb-redo')
parser.add_argument('--url', help='The PDB-REDO services URL', default=PDBREDO_URI)
subparsers = parser.add_subparsers(help='This script implements the commands login, submit, status and fetch')

parser_login = subparsers.add_parser('login', help='The login command is used to generate an access token')
parser_login.add_argument('--username', help='The user name for a valid PDB-REDO account', required=True)
parser_login.add_argument('--password', help='The password for a valid PDB-REDO account', required=False)
parser_login.add_argument('--name', help='A name for the newly created token', required=False, default="pdb-redo.py-token")
parser_login.set_defaults(func=do_login)

parser_submit = subparsers.add_parser('submit', help='The submit command is used to submit a PDB-REDO job')
parser_submit.add_argument('--token-id', help='The token ID that gives access to the PDB-REDO services', required=True)
parser_submit.add_argument('--token-secret', help='The token secret that gives access to the PDB-REDO services', required=True)
parser_submit.add_argument('--xyzin', help='The coordinates file', required=True)
parser_submit.add_argument('--hklin', help='The diffraction data file', required=True)
parser_submit.add_argument('--restraints', help='The restraints data file', required=False)
parser_submit.add_argument('--sequence', help='The sequence data file', required=False)
parser_submit.add_argument('--paired', help='Do a paired refinement', action='store_true')

# Other options you might want to set (not implemented yet):
# 
# nohyd       : do not add hydrogens (in riding postions) during refinement
# legacy      : for legacy PDB entries. R-factor is not checked and the number of
#                 refinement cycles is increased (a lot)
# notls       : no TLS refinement is performed
# notlsupdate : use TLS, but do not update the tensors in the final refinement
# noncs       : no NCS restraints are applied
# nojelly     : switch off jelly body refinement
# notwin      : no detwinning is performed
# newmodel    : always take an updated model from the re-refinement for the rebuilding steps. Only use this option when all else fails
# tighter     : try tighter restraints than usual 
# looser      : try looser restraints than usual 
# noloops     : do not try to complete loops
# nofixdmc    : do not add missing backbone atoms
# nopepflip   : no peptide flips are performed
# noscbuild   : side chains will not be rebuilt
# nocentrifuge: waters with poor density will not be deleted
# nosugarbuild: no (re)building of carbohydrates
# norebuild   : all rebuilding steps are skipped
# noanomalous : ignore all anomalous data if Fmean or Imean are available
# fewrefs     : deals with very small data sets by switching off R-free set sanity checks
# crossval    : performs (very lengthy) k-fold cross validation on the final results
# intens      : force the use of the intensities from the reflection file
# noocc       : do not refine occupancies
# notruncate  : do not use truncate to convert intensities to amplitudes
# nosigma     : do not use sigF or sigI for scaling etc.
# nometalrest : do not generate special metal restraints
# nohomology  : do not use homology-based restraints
# homology    : force homology-based restraints
# hbondrest   : use hydrogen bond restraints
# paired      : force paired refinement
# isotropic   : force isotropic B-factors

parser_submit.set_defaults(func=do_submit)


parser_status = subparsers.add_parser('status', help='Retrieve the status of a submitted PDB-REDO job')
parser_status.add_argument('--token-id', help='The token ID that gives access to the PDB-REDO services', required=True)
parser_status.add_argument('--token-secret', help='The token secret that gives access to the PDB-REDO services', required=True)
parser_status.add_argument('--job-id', help='The job ID', required=True)
parser_status.set_defaults(func=do_status)

parser_fetch = subparsers.add_parser('fetch', help='Retrieve the results of a finished PDB-REDO job')
parser_fetch.add_argument('--token-id', help='The token ID that gives access to the PDB-REDO services', required=True)
parser_fetch.add_argument('--token-secret', help='The token secret that gives access to the PDB-REDO services', required=True)
parser_fetch.add_argument('--job-id', help='The job ID', required=True)
parser_fetch.add_argument('--output', help='Where to write the output', required=False)
parser_fetch.set_defaults(func=do_fetch)

args = parser.parse_args()
args.func(args)

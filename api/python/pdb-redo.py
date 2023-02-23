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

# The default server
PDBREDO_URI = 'https://pdb-redo.eu'

PARAMETERS = {
    'nohyd', 'legacy', 'notls', 'notlsupdate', 'noncs', 'nojelly', 'notwin', 'newmodel', 'tighter', 'looser', 'noloops', 'nofixdmc',
    'nopepflip', 'noscbuild', 'nocentrifuge', 'nosugarbuild', 'norebuild', 'noanomalous', 'fewrefs', 'crossval', 'intens', 'noocc',
    'notruncate', 'nosigma', 'nometalrest', 'nohomology', 'homology', 'hbondrest', 'paired', 'isotropic'
}

# This script implements these 'commands':
#
# submit
# status
# fetch

def usage():
    print("Usage: python pdb-redo.py command")
    print("where command is one of submit, status, fetch")
    exit(1)


def do_submit(args):
    # The token id and secret for a session at PDB-REDO
    token_id = args.token_id
    token_secret = args.token_secret

    # The authentication object, used by the requests module
    auth = PDBRedoAPIAuth(token_id, token_secret)

    # The files to submit
    xyzin = args.xyzin
    hklin = args.hklin

    files = {
        'pdb-file': open(xyzin, 'rb'),
        'mtz-file': open(hklin, 'rb')
    }

    if (args.restraints != None):
        files['restraints-file'] = open(args.restraints, 'rb')

    if (args.sequence != None):
        files['sequence-file'] = open(args.sequence, 'rb')

    # Optional parameters:
    params = {
    }

    for param in args.__dict__:
        if not param in PARAMETERS:
            continue
        params[param] = args.__dict__[param]

    # Create a new job/run
    r = requests.post(args.url + "/api/run".format(
        token_id=token_id), auth=auth, files=files, data={'parameters': json.dumps(params)})
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

    r = requests.get(args.url + "/api/run/{run_id}".format(
        token_id=token_id, run_id=run_id), auth=auth)
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
    r = requests.get(args.url + "/api/run/{run_id}/output/zipped".format(
        token_id=token_id, run_id=run_id), auth=auth)
    r.raise_for_status()

    output = args.output
    if (output == None):
        output = 'pdb-redo-result-{job_id}.zip'.format(job_id=run_id)

    with open(output, 'wb') as f:
        f.write(r.content)


parser = argparse.ArgumentParser(prog='pdb-redo')
parser.add_argument(
    '--url', help='The PDB-REDO services URL', default=PDBREDO_URI)
subparsers = parser.add_subparsers(
    help='This script implements the commands login, submit, status and fetch')

parser_submit = subparsers.add_parser(
    'submit', help='The submit command is used to submit a PDB-REDO job')
parser_submit.add_argument(
    '--token-id', help='The token ID that gives access to the PDB-REDO services', required=True)
parser_submit.add_argument(
    '--token-secret', help='The token secret that gives access to the PDB-REDO services', required=True)
parser_submit.add_argument(
    '--xyzin', help='The coordinates file', required=True)
parser_submit.add_argument(
    '--hklin', help='The diffraction data file', required=True)
parser_submit.add_argument(
    '--restraints', help='The restraints data file', required=False)
parser_submit.add_argument(
    '--sequence', help='The sequence data file', required=False)

# Job parameters
parser_submit.add_argument(
    '--nohyd',			help='do not add hydrogens (in riding postions) during refinement', action='store_true')
parser_submit.add_argument(
    '--legacy',			help='for legacy PDB entries. R-factor is not checked and the number of refinement cycles is increased (a lot)', action='store_true')
parser_submit.add_argument(
    '--notls',			help='no TLS refinement is performed', action='store_true')
parser_submit.add_argument(
    '--notlsupdate',		help='use TLS, but do not update the tensors in the final refinement', action='store_true')
parser_submit.add_argument(
    '--noncs',			help='no NCS restraints are applied', action='store_true')
parser_submit.add_argument(
    '--nojelly',			help='switch off jelly body refinement', action='store_true')
parser_submit.add_argument(
    '--notwin',			help='no detwinning is performed', action='store_true')
parser_submit.add_argument(
    '--newmodel',		help='always take an updated model from the re-refinement for the rebuilding steps. Only use this option when all else fails', action='store_true')
parser_submit.add_argument(
    '--tighter',			help='try tighter restraints than usual ', type=int, default=0)
parser_submit.add_argument(
    '--looser',			help='try looser restraints than usual ', type=int, default=0)
parser_submit.add_argument(
    '--noloops',			help='do not try to complete loops', action='store_true')
parser_submit.add_argument(
    '--nofixdmc',		help='do not add missing backbone atoms', action='store_true')
parser_submit.add_argument(
    '--nopepflip',		help='no peptide flips are performed', action='store_true')
parser_submit.add_argument(
    '--noscbuild',		help='side chains will not be rebuilt', action='store_true')
parser_submit.add_argument(
    '--nocentrifuge',	help='waters with poor density will not be deleted', action='store_true')
parser_submit.add_argument(
    '--nosugarbuild',	help='no (re)building of carbohydrates', action='store_true')
parser_submit.add_argument(
    '--norebuild',		help='all rebuilding steps are skipped', action='store_true')
parser_submit.add_argument(
    '--noanomalous',		help='ignore all anomalous data if Fmean or Imean are available', action='store_true')
parser_submit.add_argument(
    '--fewrefs',			help='deals with very small data sets by switching off R-free set sanity checks', action='store_true')
parser_submit.add_argument(
    '--crossval',		help='performs (very lengthy) k-fold cross validation on the final results', action='store_true')
parser_submit.add_argument(
    '--intens',			help='force the use of the intensities from the reflection file', action='store_true')
parser_submit.add_argument(
    '--noocc',			help='do not refine occupancies', action='store_true')
parser_submit.add_argument(
    '--notruncate',		help='do not use truncate to convert intensities to amplitudes', action='store_true')
parser_submit.add_argument(
    '--nosigma',			help='do not use sigF or sigI for scaling etc.', action='store_true')
parser_submit.add_argument(
    '--nometalrest',		help='do not generate special metal restraints', action='store_true')
parser_submit.add_argument(
    '--nohomology',		help='do not use homology-based restraints', action='store_true')
parser_submit.add_argument(
    '--homology',		help='force homology-based restraints', action='store_true')
parser_submit.add_argument(
    '--hbondrest',		help='use hydrogen bond restraints', action='store_true')
parser_submit.add_argument(
    '--paired',			help='force paired refinement', action='store_true')
parser_submit.add_argument(
    '--isotropic',		help='force isotropic B-factors', action='store_true')
parser_submit.set_defaults(func=do_submit)

parser_status = subparsers.add_parser(
    'status', help='Retrieve the status of a submitted PDB-REDO job')
parser_status.add_argument(
    '--token-id', help='The token ID that gives access to the PDB-REDO services', required=True)
parser_status.add_argument(
    '--token-secret', help='The token secret that gives access to the PDB-REDO services', required=True)
parser_status.add_argument('--job-id', help='The job ID', required=True)
parser_status.set_defaults(func=do_status)

parser_fetch = subparsers.add_parser(
    'fetch', help='Retrieve the results of a finished PDB-REDO job')
parser_fetch.add_argument(
    '--token-id', help='The token ID that gives access to the PDB-REDO services', required=True)
parser_fetch.add_argument(
    '--token-secret', help='The token secret that gives access to the PDB-REDO services', required=True)
parser_fetch.add_argument('--job-id', help='The job ID', required=True)
parser_fetch.add_argument(
    '--output', help='Where to write the output', required=False)
parser_fetch.set_defaults(func=do_fetch)

args = parser.parse_args()
args.func(args)

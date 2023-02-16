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
import requests
from PDBRedoAPIAuth import PDBRedoAPIAuth
import argparse
import time

# Due to a bug? in the server implementation, the :port is required here...
PDBREDO_URI = 'https://services.pdb-redo.eu:443'

# collect arguments
parser = argparse.ArgumentParser()
parser.add_argument('--token-id', help='The token ID', required=True)
parser.add_argument('--token-secret', help='The token secret', required=True)
parser.add_argument('--xyzin', help='The coordinates file', required=True)
parser.add_argument('--hklin', help='The diffraction data file', required=True)
parser.add_argument('--paired', help='Do a paired refinement', action='store_true')

args = parser.parse_args()

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

# Optional parameters, currently there's only one:
params = {
    'paired': paired
}

# Other options you might want to set:
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


# Create a new job/run
r = requests.post(PDBREDO_URI + "/api/run", auth = auth, files = files, data = {'parameters': json.dumps(params)})
if (not r.ok):
    raise ValueError('Could not submit job to server: ' + r.text)

run_id = r.json()['id']
print("Job submitted with id", run_id)

# Loop until job is done
while(True):
    r = requests.get(PDBREDO_URI + "/api/run/{run_id}".format(run_id = run_id), auth = auth)
    status = r.json()['status']
    
    if (status == 'stopped'):
        raise ValueError('The job somehow failed after submitting')

    if (status == 'ended'):
        break

    print("Job status is", status)
    time.sleep(5)


# Get the list of files produced in the output directory
r = requests.get(PDBREDO_URI + "/api/run/{run_id}/output".format(run_id = run_id), auth = auth)

if (not r.ok):
    raise ValueError("Failed to receive the output file list")

for file in r.json():
	print(file)

# Retrieve a single result file. Here you would probably like to retrieve more files
r = requests.get(PDBREDO_URI + "/api/run/{run_id}/output/process.log".format(run_id = run_id), auth = auth)

if (not r.ok):
    raise ValueError("Failed to receive the process log")

print(r.text)
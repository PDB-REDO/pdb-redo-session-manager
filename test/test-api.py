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

def main():

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
    auth = PDBRedoAPIAuth(token_id, token_secret)

    # The files to submit
    xyzin = args.xyzin
    hklin = args.hklin
    paired = args.paired

    files = {
        'pdb-file': open(xyzin, 'rb'),
        'mtz-file': open(hklin, 'rb')
    }

    params = {
        'paired': paired
    }

    r = requests.post(PDBREDO_URI + "/api/session/{token_id}/run".format(token_id = token_id), auth = auth, files = files, data = {'parameters': json.dumps(params)})
    if (not r.ok):
        raise ValueError('Could not submit job to server: ' + r.text)

    run_id = r.json()['id']
    print("Job submitted with id", run_id)

    while(True):
        r = requests.get(PDBREDO_URI + "/api/session/{token_id}/run/{run_id}".format(token_id = token_id, run_id = run_id), auth = auth)
        status = r.json()['status']
        
        if (status == 'stopped'):
            raise ValueError('The job somehow failed after submitting')

        if (status == 'ended'):
            break

        print("Job status is", status)
        time.sleep(5)

    r = requests.get(PDBREDO_URI + "/api/session/{token_id}/run/{run_id}/output/process.log".format(token_id = token_id, run_id = run_id), auth = auth)

    if (not r.ok):
        raise ValueError("Failed to receive the process log")

    print(r.text)

main()
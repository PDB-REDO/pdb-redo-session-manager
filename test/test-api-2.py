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
parser.add_argument('--run-id', help='The run ID', required=True)
parser.add_argument('--url', help='Alternate URL to use', default=PDBREDO_URI)

args = parser.parse_args()

# The token id and secret for a session at PDB-REDO    
token_id = args.token_id
token_secret = args.token_secret

# The authentication object, used by the requests module
auth = PDBRedoAPIAuth(token_id, token_secret)

url = args.url
run_id = args.run_id

# Get the list of files produced in the output directory
r = requests.get(url + "/api/session/{token_id}/run/{run_id}/output".format(token_id = token_id, run_id = run_id), auth = auth)

if (not r.ok):
    raise ValueError("Failed to receive the output file list")

for file in r.json():
	print(file)

# Retrieve a single result file. Here you would probably like to retrieve more files
r = requests.get(url + "/api/session/{token_id}/run/{run_id}/output/process.log".format(token_id = token_id, run_id = run_id), auth = auth)

if (not r.ok):
    raise ValueError("Failed to receive the process log")

print(r.text)

# Retrieve zip
r = requests.get(url + "/api/session/{token_id}/run/{run_id}/output/zipped".format(token_id = token_id, run_id = run_id), auth = auth)

if (not r.ok):
    raise ValueError("Failed to receive the zipped file")

with open('/tmp/result.zip', 'wb') as f:
	f.write(r.content)
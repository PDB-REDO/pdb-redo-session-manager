#!/usr/bin/perl
#
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

use strict;
use warnings;

use JSON;

use FindBin;
use lib "$FindBin::RealBin/../lib";
use PDBRedo::Api();

# ---------------------------------------------------------------------

my $PDB_REDO_ADDRESS = 'https://services.pdb-redo.eu/';

my %token = (
	id => 33, secret => 'y9TbhVUuM6-JRnfidiF1Bw'
);

my $ua = PDBRedo::Api->new(
	protocols_allowed	=> ['http', 'https'],
	timeout				=> 10,
	
	token_id			=> $token{id},
	token_secret		=> $token{secret}
);

$ua->env_proxy;

my %params = ('paired' => 0);

$ua->get("${PDB_REDO_ADDRESS}api/session/${token{id}}/run/23");
exit;


my $response = $ua->post("${PDB_REDO_ADDRESS}api/session/${token{id}}/run",
	Content_Type => 'form-data',
	Content => [
		'pdb-file'		=> [ '/tmp/1cbs/1cbs.cif.gz' ],
		'mtz-file'		=> [ '/tmp/1cbs/1cbs_map.mtz' ],
		'parameters'	=> encode_json(\%params)
	]);

die "Could not submit job: " . $response->status_line unless $response->is_success;

my $r = decode_json($response->decoded_content);
my $runID = $r->{id};
my $status = $r->{status};

printf "Job with id %d has now status '%s'\n", $runID, $status;

while (1)
{
	sleep(5);

	$response = $ua->get("${PDB_REDO_ADDRESS}api/session/${token{id}}/run/${runID}");
	die "Failed to get run status: " . $response->status_line unless $response->is_success;

	$r = decode_json($response->decoded_content);

	printf "Job with id %d has now status '%s'\n", $runID, $r->{status};

	last if $r->{status} eq 'stopped' or $r->{status} eq 'ended';
}

$response = $ua->get("${PDB_REDO_ADDRESS}api/session/${token{id}}/run/${runID}/output/process.log");

die "Could not retrieve the process log" unless $response->is_success;

print $response->decoded_content, "\n";

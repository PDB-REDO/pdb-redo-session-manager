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
use Getopt::Long;
use FindBin;
use lib "$FindBin::RealBin/lib";
use PDBRedo::Api();

# ---------------------------------------------------------------------

sub usage()
{
	print "Usage: pdb-redo.pl <command> [options]\n" .
	  "  where command is one of submit, status, fetch\n\n";
	exit(0);
}

my $PDB_REDO_ADDRESS = 'https://services.pdb-redo.eu/';

# The submit call

sub do_submit()
{
	my ($token_id, $token_secret, $url, $xyzin, $hklin, %params);
	$url = $PDB_REDO_ADDRESS;

	GetOptions('url=s' => \$url, 'token-id=i' => \$token_id, 'token-secret=s' => \$token_secret,
		'xyzin=s' => \$xyzin, 'hklin=s' => \$hklin);

	die "Please specify the parameters\n" unless defined($url) and defined($token_id) and defined($token_secret) and defined($xyzin) and defined($hklin);

	my $ua = PDBRedo::Api->new(
		protocols_allowed	=> ['http', 'https'],
		timeout				=> 10,
		
		token_id			=> $token_id,
		token_secret		=> $token_secret
	);

	my $response = $ua->post("${url}api/run",
		Content_Type => 'form-data',
		Content => [
			'pdb-file'		=> [ $xyzin ],
			'mtz-file'		=> [ $hklin ],
			'parameters'	=> encode_json(\%params)
		]);

	die "Could not submit job: " . $response->status_line unless $response->is_success;

	my $r = decode_json($response->decoded_content);
	my $runID = $r->{id};
	my $status = $r->{status};

	printf "Job with id %d has now status '%s'\n", $runID, $status;
}

# The status call
sub do_status()
{
	my ($token_id, $token_secret, $url, $job_id);
	$url = $PDB_REDO_ADDRESS;

	GetOptions('url=s' => \$url, 'token-id=i' => \$token_id, 'token-secret=s' => \$token_secret, 'run-id=i' => \$job_id);

	die "Please specify the parameters\n" unless defined($url) and defined($token_id) and defined($token_secret) and defined($job_id);

	my $ua = PDBRedo::Api->new(
		protocols_allowed	=> ['http', 'https'],
		timeout				=> 10,
		
		token_id			=> $token_id,
		token_secret		=> $token_secret
	);

	my $response = $ua->get("${url}api/run/${job_id}");
	die "Failed to get run status: " . $response->status_line unless $response->is_success;

	my $r = decode_json($response->decoded_content);

	printf "Job with id %d has status '%s'\n", $job_id, $r->{status};
}

# The fetch call

sub do_fetch()
{
	my ($token_id, $token_secret, $url, $job_id);
	$url = $PDB_REDO_ADDRESS;

	GetOptions('url=s' => \$url, 'token-id=i' => \$token_id, 'token-secret=s' => \$token_secret, 'run-id=i' => \$job_id);

	die "Please specify the parameters\n" unless defined($url) and defined($token_id) and defined($token_secret) and defined($job_id);

	my $ua = PDBRedo::Api->new(
		protocols_allowed	=> ['http', 'https'],
		timeout				=> 10,
		
		token_id			=> $token_id,
		token_secret		=> $token_secret
	);

	my $response = $ua->get("${url}api/run/${job_id}/output/zipped");
	die "Failed to get run status: " . $response->status_line unless $response->is_success;

    my $content = $response->decoded_content;
    my $out_file = "pdb-result-file-${job_id}.zip";

    open my $out_fh, '>', $out_file or die qq{Unable to open "$out_file" for output: $!};
    print $out_fh $content;
    close $out_fh or die qq{Unable to close "$out_file": $!};
}

# main

my $command = shift @ARGV;

     if ($command eq 'submit') {
	do_submit();
} elsif ($command eq 'status') {
	do_status();
} elsif ($command eq 'fetch') {
	do_fetch();
} else { usage(); }

exit;


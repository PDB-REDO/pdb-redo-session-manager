#!/usr/bin/perl

use strict;
use warnings;

use JSON;

use FindBin;
use lib "$FindBin::RealBin/../lib";
use PDBRedo::Api();

# ---------------------------------------------------------------------

my $PDB_REDO_ADDRESS = 'http://localhost:10339/';

my %token = (
	id => 23, secret => 'DB_MOiltiSA-P5j-d41IKg'
);

my $ua = PDBRedo::Api->new(
	protocols_allowed	=> ['http', 'https'],
	timeout				=> 10,
	
	token_id			=> $token{id},
	token_secret		=> $token{secret}
);

$ua->env_proxy;

my %params = ('paired' => 0);

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

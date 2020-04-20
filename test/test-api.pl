#!/usr/bin/perl

use strict;
use warnings;

use FindBin;
use lib "$FindBin::RealBin/../lib";
use PDBRedo::Api();

# ---------------------------------------------------------------------

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

my $response = $ua->post("http://localhost:10339/api/session/${token{id}}/run",
	Content_Type => 'form-data',
	Content => [
		'pdb-file' => [ '/tmp/1cbs/1cbs.cif.gz' ],
		'mtz-file' => [ '/tmp/1cbs/1cbs_map.mtz' ]
	]);


if ($response->is_success) {
	print $response->decoded_content;
}
else {
	die $response->status_line;
}

$response = $ua->get("http://localhost:10339/api/session/${token{id}}/run");

if ($response->is_success) {
	print $response->decoded_content;
}
else {
	die $response->status_line;
}

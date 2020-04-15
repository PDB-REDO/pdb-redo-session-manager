#!/usr/bin/perl

use strict;
use warnings;

use FindBin;
use lib "$FindBin::RealBin/../lib";
use PDBRedo::Api();
use Data::Dumper;

# ---------------------------------------------------------------------

my %token = (
	id => 15, secret => 'CQYTJ5fv1ZyPQGE2h/ph7A=='
);

my $ua = PDBRedo::Api->new(
	protocols_allowed	=> ['http', 'https'],
	timeout				=> 10,
	
	token_id			=> $token{id},
	token_secret		=> $token{secret}
);

$ua->env_proxy;

my $response = $ua->get("http://localhost:10339/api/session/${token{id}}/run");

if ($response->is_success) {
	print $response->decoded_content;
}
else {
	die $response->status_line;
}

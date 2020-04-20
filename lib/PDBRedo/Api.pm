package PDBRedo::Api;

use strict;
use warnings;

use Digest::SHA qw(sha256 sha256_base64 hmac_sha256 hmac_sha256_base64);
use URI;
use LWP::UserAgent;
use POSIX qw(strftime);

use parent "LWP::UserAgent";
use Data::Dumper;

sub new
{
	my $class = shift;
	
	my (%args) = @_;
	
	my $self = $class->SUPER::new(@_);

	$self->{token_id} = $args{'token_id'};
	$self->{token_secret} = $args{'token_secret'};
	
	$self->add_handler('request_prepare', \&sign_request);

	return $self;
};

sub pad
{
	my $b64_digest = shift;
	while (length($b64_digest) % 4) {
		$b64_digest .= '=';
	}
	return $b64_digest;
}

sub sign_request
{
	my ($request, $ua, $handler) = @_;
	
	die "No token defined!" unless defined $ua->{'token_id'} and defined $ua->{'token_secret'};
	
	my $tokenID = $ua->{token_id};
	my $tokenSecret = $ua->{token_secret};
	
	my $contentDigest = pad(sha256_base64($request->content));

	print STDERR $contentDigest, "\n";

	my $uri = URI->new($request->uri);
	my $path = $uri->path;
	my $host = $uri->host;
	my $port = $uri->port;
	$host = "$host:$port" unless $port == 80;
	my $query = $uri->query;	$query = '' unless $query;

	my $canonicalRequest = join("\n", $request->method, $path, $query, $host, $contentDigest);
	my $canonicalRequestHash = pad(sha256_base64($canonicalRequest));

	my $now = time();
	my $timestamp = strftime('%Y-%m-%dT%H:%M:%SZ', gmtime($now));
	my $date = strftime('%Y%m%d', gmtime($now));
	
	my $credential = "${tokenID}/${date}/pdb-redo-api";
	my $stringToSign = join("\n", "PDB-REDO-api", $timestamp, $credential, $canonicalRequestHash);
	my $key = hmac_sha256($date, "PDB-REDO${tokenSecret}");

	my $signature = pad(hmac_sha256_base64($stringToSign, $key));
	
	$request->header('X-PDB-REDO-Date', $timestamp);
	$request->header('Authorization',
		"PDB-REDO-api Credential=${credential},SignedHeaders=host;x-pdb-redo-content,Signature=${signature}");
}

1;

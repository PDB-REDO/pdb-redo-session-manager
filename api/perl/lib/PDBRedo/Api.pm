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

	my $uri = URI->new($request->uri);
	my $path = $uri->path;
	my $host = $uri->host;
	my $port = $uri->port;
	$host = "$host:$port" if (defined($port) and $port != 80 and $port != 443);
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

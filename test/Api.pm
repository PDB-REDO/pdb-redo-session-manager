package PDBRedo::Api;

use strict;
use warnings;

use LWP::UserAgent;

our @ISA = "LWP::UserAgent";

sub new
{
	my $invocant = shift;
	my $self = new LWP::UserAgent(
		@_
	);
	return bless $self, "PDBRedo::Api";

};

1;

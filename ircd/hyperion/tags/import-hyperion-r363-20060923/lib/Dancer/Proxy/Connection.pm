# This file is copyright (C) 2001 Andrew Suffield
#                                  <asuffield@freenode.net>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA


package Dancer::Proxy::Connection;

use strict;
use warnings;

use IO::Select;
use IO::Socket;
use Fcntl;
use POSIX;
use Tie::RefHash;
use Carp;

my $select = IO::Select->new();
my %ports = ();
my %socket = ();

sub _nonblock
  {
    my $socket = shift;

    my $flags = fcntl($socket, F_GETFL, 0)
      or die "Can't get flags for socket: $!\n";
    fcntl($socket, F_SETFL, $flags | O_NONBLOCK)
      or die "Can't set O_NONBLOCK on socket: $!\n";
  }

sub new_listener
  {
    my $self = {};
    my $port = shift;
    my $addr = shift;
    $self->{SOCKET} = IO::Socket::INET->new(LocalPort => $port,
					  Listen => SOMAXCONN,
					  ReuseAddr => 1,
					  (defined $addr ? (LocalAddr => $addr) : ()),
					  Type => SOCK_STREAM)
      or carp("Couldn't create a listener on port $port: $@\n"), return undef;
    _nonblock($self->{SOCKET});
    $select->add($self->{SOCKET});
    $socket{$self->{SOCKET}} = $self;
    $self->{TYPE} = 'listener';
    $self->{INBUF} = '';
    $self->{OUTBUF} = '';
    $self->{EVENTS} = {map {$_ => []} qw/accept message/};
    bless ($self);
    return $self;
  }

sub new_connection
  {
    my $self = {};
    my $peer = shift;
    my $port = shift;
    $self->{SOCKET} = IO::Socket::INET->new(PeerAddr => $peer,
					  PeerPort => $port)
      or carp("Couldn't create a connection to $peer:$port: $@\n"), return undef;
    _nonblock($self->{SOCKET});
    $select->add($self->{SOCKET});
    $socket{$self->{SOCKET}} = $self;
    $self->{TYPE} = 'connected';
    $self->{INBUF} = '';
    $self->{OUTBUF} = '';
    $self->{EVENTS} = {map {$_ => []} qw/accept message/};
    bless ($self);
    return $self;
  }

sub _accept_connection
  {
    my $self = {};
    my $sock = shift;
    _nonblock($sock);
    $select->add($sock);
    $self->{SOCKET} = $sock;
    $self->{TYPE} = 'connected';
    $self->{INBUF} = '';
    $self->{OUTBUF} = '';
    $self->{EVENTS} = {map {$_ => []} qw/accept message/};
    $socket{$sock} = $self;
    bless $self;
    return $self;
  }

sub register
  {
    my $self = shift;
    my $event = shift;
    my $sub = shift;
    push @{$self->{EVENTS}{$event}}, {sub => $sub, parms => \@_};
  }

sub unregister
  {
    my $self = shift;
    my $event = shift;
    @{$self->{EVENTS}{$event}} = grep {$_ != $event} @{$self->{EVENTS}{$event}};
  }

sub type
  {
    my $self = shift;
    return $self->{TYPE};
  }

sub close
  {
    my $self = shift;
    my $reason = shift || "";
    return if $self->{DESTROYING};
    $self->{DESTROYING}++;
    my $socket = $self->{SOCKET};
    undef $self->{SOCKET};
    foreach my $handler (@{$self->{EVENTS}{close}})
      {
	$handler->{sub}->($self, $handler->{parms});
      }
    $select->remove($socket);
    if (defined $socket)
      {
	# Don't want to know about it if these fail
	eval
	  {
	    $socket->send("ERROR :$reason\r\n");
	    $socket->close;
	  };
	undef $socket;
      }
    # Delete this to protect against loops
    delete $self->{EVENTS};
    delete $socket{$self};
  }

sub DESTROY
  {
    my $self = shift;
    $self->close();
  }

sub send
  {
    my $self = shift;
    my $data = shift;
    $self->{OUTBUF} .= $data;
  }

sub poll
  {
    my %ready = ();
    tie %ready, 'Tie::RefHash';
    return 0 unless $select->count;
    foreach my $s ($select->can_read(1))
      {
	my $conn = $socket{$s};
	if ($conn->{TYPE} eq 'listener')
	  {
	    my $new_sock = $s->accept();
	    if (defined $new_sock)
	      {
		my $new_conn = _accept_connection $new_sock;
		HANDLER: foreach my $handler (@{$conn->{EVENTS}{accept}})
		  {
		    last HANDLER unless defined $handler->{sub}->($new_conn, $conn, $handler->{parms});
		  }
	      }
	  }
	if ($conn->{TYPE} eq 'connected')
	  {
	    my $data = '';
	    # <sigh> it's possible that the socket was wiped out earlier in this loop
	    next unless defined $conn->{SOCKET};
	    my $rv = $conn->{SOCKET}->recv($data, POSIX::BUFSIZ, 0);

	    unless (defined $rv and length $data)
	      {
		# Bad, connection might as well be closed
		$conn->close("Read error");
		next;
	      }
	    $conn->{INBUF} .= $data;
	    while ($conn->{INBUF} =~ s/^(.*?\r?\n)//)
	      {
		push @{$ready{$conn}}, $1;
	      }
	  }
      }
    foreach my $conn (keys %ready)
      {
	MESSAGE: foreach my $msg (@{$ready{$conn}})
	  {
	    foreach my $handler (@{$conn->{EVENTS}{message}})
	      {
		last MESSAGE unless defined $handler->{sub}->($msg, $handler->{parms});
	      }
	  }
      }
    foreach my $s ($select->can_write(1))
      {
	my $conn = $socket{$s};
	next unless length $conn->{OUTBUF};

	my $rv = $conn->{SOCKET}->send($conn->{OUTBUF}, 0);
	unless (defined $rv)
	  {
	    carp "Select said I could write, but send failed\n";
	    next;
	  }

	if ($rv == length $conn->{OUTBUF} || $! == POSIX::EWOULDBLOCK)
	  {
	    # Delete $rv characters from the buffer, we sent them
	    substr($conn->{OUTBUF}, 0, $rv) = '';
	  }
	else
	  {
	    # Something went wrong
	    $conn->close("Write error");
	  }
      }
    return 1;
  }

1;

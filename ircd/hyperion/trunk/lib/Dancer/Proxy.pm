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


package Dancer::Proxy;

use strict;
use warnings;
use Dancer::Proxy::Connection;
use Carp;

my %server = ();

sub poll
  {
    Dancer::Proxy::Connection::poll @_;
  }

sub add_server
  {
    my ($name, $peer, $port) = @_;
    $server{$name} = {peer => $peer, port => $port};
  }

sub listen
  {
    my ($port,$addr) = @_;
    my $self = {};
    my $listener = Dancer::Proxy::Connection::new_listener($port, $addr);
    $listener->register('accept', \&_handle_accept, $self);
    $listener->register('close', \&_handle_close, $self);
    $self->{CONN} = $listener;
    $self->{EVENTS} = {map {$_ => []} qw/accept message/};
    bless($self);
    return $self;
  }

sub _handle_accept
  {
    my ($conn, $listener, $parms) = @_;
    my ($listener_self) = @{$parms};
    my $self = {};
    $self->{CONN} = $conn;
    $self->{EVENTS} = {map {$_ => []} qw/accept message/};
    bless($self);
    $conn->register('message', \&_handle_message, $self);
    $conn->register('close', \&_handle_close, $self);
    foreach my $handler (@{$listener_self->{EVENTS}{accept}})
      {
	return undef unless $handler->{sub}->($self, $listener_self, $handler->{parms});
      }
    return 1;
  }

sub register
  {
    my $self = shift;
    my $event = shift;
    my $sub = shift;
    push @{$self->{EVENTS}{$event}}, {sub => $sub, parms => \@_};
  }

sub send
  {
    my $self = shift;
    $self->{CONN}->send(@_);
  }

sub peer
  {
    my $self = shift;
    return exists $self->{PEER} ? $self->{PEER} : undef;
  }

sub name
  {
    my $self = shift;
    return $self->{NAME};
  }

sub _handle_message
  {
    my ($msg, $parms) = @_;
    my ($self) = @{$parms};
    $msg =~ s/\r?\n$//;
    my ($prefix, $command, @args) = parse($msg);
    foreach my $handler (@{$self->{EVENTS}{message}})
      {
	my $new_msg = $handler->{sub}->($self, $prefix, $command, \@args, $handler->{parms});
	return undef unless defined $new_msg;
	if ($new_msg ne $msg and $new_msg ne '')
	  {
	    ($prefix, $command, @args) = parse ($msg = $new_msg);
	  }
      }
    if ($command eq 'CHALL' and defined $args[1] and not exists $self->{PEER})
      {
	my ($from, $to) = @args;
	$self->{NAME} = $from;
	unless ($server{$to})
	  {
	    carp "$from asking for $to, which I don't know about\n";
	    $self->close("Tried to connect to unknown server");
	    return undef;
	  }
	my $new_conn = Dancer::Proxy::Connection::new_connection($server{$to}{peer}, $server{$to}{port});
	unless (defined $new_conn)
	  {
	    $self->close("Failed to connect to $to");
	    return undef;
	  }
	my $new_self = {CONN => $new_conn,
			EVENTS => {map {$_ => ()} ('accept', 'message')},
			PEER => $self,
			NAME => $to};
	bless($new_self);
	$new_conn->register('message', \&_handle_message, $new_self);
	$new_conn->register('close', \&_handle_close, $self);
	$self->{PEER} = $new_self;
	foreach my $handler (@{$self->{EVENTS}{connect}})
	  {
	    unless ($handler->{sub}->($self, $new_self, $server{$to}{peer}, $server{$to}{port}, $handler->{parms}))
	      {
		$self->close;
		return undef;
	      }
	  }
      }
    $self->{PEER}{CONN}->send("$msg\r\n") if exists $self->{PEER};
    return 1;
  }

sub parse
  {
    my $line = shift;
    my ($prefix, $command, $args);
    if ($line =~ /^:(\S*) (.*)$/)
      {
	$prefix = $1;
	my $rest = $2;
	if ($rest =~ /(\S*) (.*)/)
	  {
	    $command = $1;
	    $args = $2;
	  }
	else
	  {
	    # Grmph. No arguments.
	    $command = $rest;
	  }
      }
    else
      {
	if ($line =~ /(\S*) (.*)/)
	  {
	    $command = $1;
	    $args = $2;
	  }
	else
	  {
	    # Grmph. No arguments.
	    $command = $line;
	  }
      }
    my @args = ();
    @args = split ' ', $args if defined $args;
    return ($prefix, $command, @args);
  }

sub unparse
  {
    my ($prefix, $command, @args) = @_;
    my $msg = '';
    $msg .= ":$prefix " if defined $prefix;
    $msg .= "$command ";
    $msg .= join ' ', @args;
    return $msg;
  }

sub _handle_close
  {
    my ($conn, $parms) = @_;
    my $self = shift @{$parms};
    $self->close() if defined $self;
  }

sub close
  {
    my $self = shift;
    return if $self->{DESTROYING};
    $self->{DESTROYING}++;
    $self->{CONN}->close(@_) if defined $self->{CONN};
    delete $self->{CONN};
    foreach my $handler (@{$self->{EVENTS}{close}})
      {
	$handler->{sub}->($self, $handler->{parms});
      }
    my $peer = $self->{PEER} || undef;
    delete $self->{PEER};
    # Delete this to protect against loops
    delete $self->{EVENTS};
    $peer->close if defined $peer;
  }

sub DESTROY
  {
    my $self = shift;
    $self->close;
  }

1;

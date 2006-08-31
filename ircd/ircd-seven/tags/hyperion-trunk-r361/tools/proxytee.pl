#!/usr/bin/perl
# proxytee.pl, proxy between a set of dancer servers, dumping the traffic
#  between them to stdout
# Based on example 17-6 (recipe 17.13) in the perl cookbook (bighorn sheep)
#
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

use warnings;
use strict;
use FindBin;
use lib "$FindBin::Bin/../lib";
use Dancer::Proxy;
use Carp;

my $port = 6667;
my %servers = ();
my %skip = map {$_ => 1} qw/PING PONG SPINGTIME/;

read_config();

my $listener = Dancer::Proxy::listen($port);
$listener->register('accept', \&accept_connection);

sub accept_connection
  {
    my ($connection, $listener) = @_;
    $connection->register('message', \&handle_message);
    $connection->register('connect', \&handle_connect);
    $connection->register('close', \&handle_close);
  }

sub handle_message
  {
    my ($connection, $prefix, $command, $args) = @_;
    unless ($skip{$command})
      {
	if (defined $connection->name and defined $connection->peer and defined $connection->peer->name)
	  {
	    print $connection->name . " -> " . $connection->peer->name;
	  }
	print " " . Dancer::Proxy::unparse($prefix, $command, @{$args}) . "\n";
      }
    return '';
  }

sub handle_connect
  {
    my ($connection, $target, $peername, $peerport) = @_;
    print "Connecting from " . $connection->name . " to " . $target->name . " ($peername:$peerport)\n";
    $target->register('message', \&handle_message);
    $target->register('close', \&handle_close);
  }

sub handle_close
  {
    my $connection = shift;
    if (defined $connection->name and defined $connection->peer and defined $connection->peer->name)
      {
	print $connection->name . " -> " . $connection->peer->name . " closing\n";
      }
  }

1 while Dancer::Proxy::poll;

# NOT REACHED
exit;

sub read_config
  {
    open(CONF, "proxy.rc")
      or die "Can't open proxy.rc: $!\n";
    while(my $line = <CONF>)
      {
	next if $line =~ /^\s*(\#.*)?$/; # Comments and lines containing only whitespace
	my ($name, $addr, $port) = $line =~ /(.*),(.*),(.*)/;
	Dancer::Proxy::add_server($name, $addr, $port);
      }
    close CONF;
  }

__END__

sub handle
  {
    my $client = shift;
    foreach my $request (@{$ready{$client}})
      {
	$request =~ s/\r?\n$//;
	my ($prefix, $command, @args) = parse($request);
	if (exists $peer{$client})
	  {
	    unless (exists $skip{$command} or ($command eq 'NOTICE' and not defined $prefix))
	      {
		my $sargs = '';
		$sargs = join ' ', (@args) if (scalar @args);

		if ($command eq "WALLOPS")
		  {
		    # Eliminate forwarded WALLOPS
		    if ($name{$client} eq $prefix)
		      {
			# Eliminate repeated WALLOPS
			unless (defined $last_wallops{$name{$client}}
				and $last_wallops{$name{$client}} eq $sargs
				and $last_wallops_to{$name{$client}} ne $name{$peer{$client}})
			  {
			    print "$name{$client} WALLOPS $sargs\n" if $name{$client} eq $prefix;
			    $last_wallops{$name{$client}} = $sargs;
			    $last_wallops_to{$name{$client}} = $name{$peer{$client}};
			  }
		      }
		  }
		else
		  {
		    my $output = "$name{$client} -> $name{$peer{$client}} ";
		    $output .= ":$prefix " if defined $prefix;
		    $output .= $command;
		    $output .= " " . $sargs if length $sargs;
		    print "$output\n";
		  }
	      }
	    $outbuffer{$peer{$client}} .= "$request\r\n";
	  }
	else
	  {
	    push @{$preconnect_buffer{$client}}, $request;
	    if ($command eq 'CHALL' and defined $args[1])
	      {
		my ($from, $to) = ($args[0], $args[1]);
		print "Server connected, claiming to be $from, asking for $to\n";
		my $target = $servers{$to} if exists $servers{$to}
		  or do
		    {
		      print "Don't have a server record for $to\n";
		      close_socket($client);
		      return;
		    };
		my $target_socket = IO::Socket::INET->new(PeerAddr => $target->{addr},
							  PeerPort => $target->{port})
		  or do
		    {
		      print "Couldn't connect to ".$target->{addr}.':'.$target->{port}.": $@\n";
		      close_socket($client);
		      return;
		    };
		nonblock($target_socket);
		print "Connected to ".$target->{addr}.':'.$target->{port}.", repeating buffer\n";

		$peer{$client} = $target_socket;
		$peer{$target_socket} = $client;
		$name{$client} = $from;
		$client_name{$from} = $client;
		$name{$target_socket} = $to;
		$client_name{$to} = $target_socket;
		$select->add($target_socket);

		foreach my $line (@{$preconnect_buffer{$client}})
		  {
		    $outbuffer{$target_socket} .= "$line\r\n";
		  }
		delete $preconnect_buffer{$client};

	      }
	  }
      }
    delete $ready{$client};
  }


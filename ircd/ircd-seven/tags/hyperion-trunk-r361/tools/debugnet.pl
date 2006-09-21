#!/usr/bin/perl
# debugnet.pl, generate a working tree of test servers to debug dancer
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
use Cwd;
use File::Copy;
use File::Path;
use Getopt::Std;

# This needs to be some command which executes a GNU make
my $make = 'make';

my $base_tree = cwd;
-e "$base_tree/src/m_map.c" or die "Couldn't find dancer source tree rooted at $base_tree, aborting\n";
my $build_tree = "$base_tree";
my $debug_tree = "$base_tree/debug-tree";
my $leaves = 8;
my $hubs = 4;
my $config = <<'EOF';
# Generated by debugnet.pl

A:debugnet:debugnet:Generated by debugnet.pl
# luser
Y:1:30:0:200:1000000
I:::*@*::1
I:debug.spoof.host:$1$nmNi3oKw$/TTB9SfKK3.KPYfKBYLy20:debugnick@*:abcs:1:cs
# leaf -> hub
Y:2:20:30:1:1000000
# hub -> leaf
Y:3:20:30:0:1000000
# hub -> hub
Y:4:20:30:2:1000000
# leaf -> leaf
Y:7:20:30:0:1000000
# oper
Y:5:20:0:20:1000000
O:*:$1$nmNi3oKw$/TTB9SfKK3.KPYfKBYLy20:admin:aAbBcdDfFgGhHkKlLmMnNpPrRsStUvVwWxXyYzZ0123459*@:5:aAbBcdDfFgGhHkKlLmMnNpPrRsStUvVwWxXyYzZ0123459*@
O:*:$1$nmNi3oKw$/TTB9SfKK3.KPYfKBYLy20:luser::5:
H:*::*

# services
Y:6:20:0:20:1000000
C:127.0.0.1:foo:services.dancer-debug::6
N:127.0.0.1:$1$uRZdBj4T$RPisyG86/WK4EgptgrtUU0:services.dancer-debug::6

EOF
my $password = 'password';
my $password_hash = '$1$uRZdBj4T$RPisyG86/WK4EgptgrtUU0';
my $proxy_port = 6667;

my $configure_parms = "";
my %args;

getopts("cehH:L:q\?", \%args);

if ($args{h} or $args{'?'})
  {
    print <<'EOF';
debugnet.pl [-c] [-H hubs] [-L leaves] [-h] [-q]
-c                 Only build config tree, not source
-e                 Link with electric fence
-h                 Show this help
-H hubs            Set the number of hubs to create (default: 2)
-L leaves          Set the number of leaf nodes to create (default: 6)
-q                 Be quiet (no output unless error)
EOF
    exit;
  }

$hubs = $args{H} if defined $args{H};
$leaves = $args{L} if defined $args{L};
$configure_parms .= " --with-efence" if defined $args{e};

build() unless $args{c};
create_tree();
unless($args{q})
  {
    print "Done\n";
    print "Change into directory $debug_tree\n";
    print "and run ./start to launch the ircds and start the proxy.\n";
  }

################################################################################

sub build
  {
    # Build the ircd
    my $pipe = '';
    $pipe = '>/dev/null' if $args{q};
    system("cd $build_tree && ./configure --enable-maintainer-mode --prefix=$debug_tree $configure_parms $pipe && $make $pipe") == 0
      or die "Build failed, aborting\n";

    # Create/check the debug tree
    mkdir $debug_tree unless -e $debug_tree;
    die "The target tree $debug_tree is not a directory!\n" unless -d $debug_tree;

    copy("$build_tree/src/dancer-ircd", "$debug_tree/ircd");
    (scalar chmod 0755, "$debug_tree/ircd") == 1
      or warn "Couldn't chmod 0755 $debug_tree/ircd\n";
  }

sub create_tree
  {
    print "Creating debug tree in $debug_tree\n" unless $args{q};
    my @names = ();
    my %port = ();
    my %hub = ();
    foreach (1..$hubs)
      {
	my $name = "hub".$_.".dancer-debug";
	push @names, $name;
	# Hubs have ports in the range 7666..7665+$hubs
	$port{$name} = 7665 + $_;
      }
    foreach (1..$leaves)
      {
	my $name = "leaf".$_.".dancer-debug";
	push @names, $name;
	# Leaves have ports in the range 8666..8665+$leaves
	$port{$name} = 8665 + $_;
	my $hub_number = int(($_ - 1) / ($leaves / $hubs));
	$hub_number++ unless $hub_number >= $hubs;
	$hub{$name} = "hub$hub_number.dancer-debug";
      }
    system("make install") == 0
      or die "make failed\n";

    open(PROXYCONF, ">$debug_tree/proxy.rc")
      or die "Can't create $debug_tree/proxy.rc: $!\n";

    # OK, make the trees
    foreach my $name (@names)
      {
	print "Creating files for $name\n" unless $args{q};
	# Clear all the old files
	rmtree "$debug_tree/$name" if -e "$debug_tree/$name";

	# Make the new tree
	mkpath ["$debug_tree/$name/etc/dancer-ircd",
		"$debug_tree/$name/sbin",
		"$debug_tree/$name/var/lib/dancer-ircd",
		"$debug_tree/$name/var/log/dancer-ircd",
		"$debug_tree/$name/var/run/dancer-ircd"];
	symlink "../../ircd", "$debug_tree/$name/sbin/dancer-ircd";

	# Create the empty files
	foreach my $file (qw!
			  etc/dancer-ircd/motd
			  etc/dancer-ircd/omotd
			  etc/dancer-ircd/ohelp
			  var/lib/dancer-ircd/kline.conf
			  var/lib/dancer-ircd/dline.conf
			  var/log/dancer-ircd/user.log
			  var/log/dancer-ircd/oper.log!)
	  {
	    open(FH, ">$debug_tree/$name/$file");
	    close(FH);
	  }

	# Create the config file
	open(CONF, ">$debug_tree/$name/etc/dancer-ircd/ircd.conf");

	# First the stub
	print CONF $config;

	# Then the M:line
	print CONF "M:$name:127.0.0.1:dancer debug server:\n";

	# Now the C/N pairs
	foreach my $peer (@names)
	  {
	    # Skip myself
	    next if $peer eq $name;
	    # What to put in the port field in the C:line?
	    my $port = "";
	    my $class;
	    if ($name =~ /^hub/)
	      {
		$port = $proxy_port if $peer =~ /^hub/;
		$class = ($peer =~ /^hub/) ? 4 : 3;
	      }
	    else
	      {
		$port = $proxy_port if $peer eq $hub{$name};
		$class = ($peer =~ /^hub/) ? 2 : 7;
	      }
	    print CONF "C:127.0.0.1:$password:$peer:$port:$class\n";
	    print CONF "N:127.0.0.1:$password_hash:$peer\::$class\n";
	    print CONF "\n";
	  }

	# And lastly the P:line
	print CONF "P::127.0.0.1::$port{$name}\n";

	close(CONF);

	# OK, write to the config for proxytee.pl
	print PROXYCONF "$name,127.0.0.1,$port{$name}\n";
      }
    close PROXYCONF;
    symlink "../tools/start_debugnet.pl", "$debug_tree/start";
    symlink "../tools/proxytee.pl", "$debug_tree/proxy";
  }
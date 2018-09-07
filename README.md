# CG-HCPCLI

Carrier grade HTTP CONNECT proxy client (CG-HCPCLI) is a client library for a
NAT traversal solution called carrier grade HTTP CONNECT proxy (CG-HCP), which
is one type of carrier grade TCP proxy (CG-TP). For an implementation of
CG-HCP, please see ldpairwall in Github under Aalto5G.

CG-HCP works by client connecting to e.g. port 8080 of the NAT middlebox and
sending:
```
CONNECT ssh.example.com:22 HTTP/1.1
Host: ssh.example.com:22

```

The NAT middlebox then redirects this connection to the machine behind it
providing SSH services. Once that is successful, it replies with:
```
HTTP/1.1 200 OK

```

CG-HCPCLI is simply a library that allows creating sockets and automatically
performing the HTTP CONNECT handshake.

The information about HTTP CONNECT path is stored in the domain name system
(DNS):

```
_cgtp.ssh.example.com. IN TXT nat.example.net:8080!ssh.example.com
```

The CG-HCPCLI library then whenever trying to connect to `ssh.example.com`
first tries to resolve `TXT? _cgtp.ssh.example.com` and only after that `AAAA?
ssh.example.com` and `A? ssh.example.com`.

The example `TXT` record for `_cgtp.ssh.example.com` means that
`ssh.example.com` is reachable by connecting to `nat.example.net` port 8080
and then using HTTP CONNECT method for connecting to `ssh.example.com`.

# Testing with network namespaces

Clone ldpairwall (into `ldpairwall/`) and cghcpcli (into `cghcpcli/`), both
from Github under Aalto5G, and execute:

```
mkdir -p /etc/netns/ns1
echo "nameserver 10.150.2.100" > /etc/netns/ns1/resolv.conf
ip link add veth0 type veth peer name veth1
ip link add veth2 type veth peer name veth3
ifconfig veth0 up
ifconfig veth1 up
ifconfig veth2 up
ifconfig veth3 up
ethtool -K veth0 rx off tx off tso off gso off gro off lro off
ethtool -K veth1 rx off tx off tso off gso off gro off lro off
ethtool -K veth2 rx off tx off tso off gso off gro off lro off
ethtool -K veth3 rx off tx off tso off gso off gro off lro off
ip netns add ns1
ip netns add ns2
ip link set veth0 netns ns1
ip link set veth3 netns ns2
ip netns exec ns1 ip addr add 10.150.2.1/24 dev veth0
ip netns exec ns2 ip addr add 10.150.1.101/24 dev veth3
ip netns exec ns1 ip link set veth0 up
ip netns exec ns2 ip link set veth3 up
ip netns exec ns1 ip link set lo up
ip netns exec ns2 ip link set lo up
ip netns exec ns2 ip route add default via 10.150.1.1
```

Then run in one terminal window and leave it running:
```
./ldpairwall/airwall/ldpairwall veth2 veth1
```

Then, execute netcat in one terminal window:
```
ip netns exec ns2 nc -v -v -v -l -p 1234
```

...and try in another window:
```
ip netns exec ns1 ./cghcpcli/clilib/clilibtest ssh.example.com 1234
```

The connection should open and close successfully in the netcat side.

# Using with OpenSSH

Add into `.ssh/config`:

```
Host ssh.example.com
        HostName        ssh.example.com
        ProxyCommand    /path/to/cghcpcli/cghcpproxycmd/cghcpproxycmd 10.150.2.100 8080 %h %p
```

Or if DNS is properly configured:

```
Host ssh.example.com
        HostName        ssh.example.com
        ProxyCommand    /path/to/cghcpcli/cghcpproxycmd/cghcpproxycmd %h %p
```

Then execute:
```
mkdir -p /etc/netns/ns1
echo "nameserver 10.150.2.100" > /etc/netns/ns1/resolv.conf
ip link add veth0 type veth peer name veth1
ip link add veth2 type veth peer name veth3
ifconfig veth0 up
ifconfig veth1 up
ifconfig veth2 up
ifconfig veth3 up
ethtool -K veth0 rx off tx off tso off gso off gro off lro off
ethtool -K veth1 rx off tx off tso off gso off gro off lro off
ethtool -K veth2 rx off tx off tso off gso off gro off lro off
ethtool -K veth3 rx off tx off tso off gso off gro off lro off
ip netns add ns1
ip netns add ns2
ip link set veth0 netns ns1
ip link set veth3 netns ns2
ip netns exec ns1 ip addr add 10.150.2.1/24 dev veth0
ip netns exec ns2 ip addr add 10.150.1.101/24 dev veth3
ip netns exec ns1 ip link set veth0 up
ip netns exec ns2 ip link set veth3 up
ip netns exec ns1 ip link set lo up
ip netns exec ns2 ip link set lo up
ip netns exec ns2 ip route add default via 10.150.1.1
```

Then run in one terminal window and leave it running:
```
./ldpairwall/airwall/ldpairwall veth2 veth1
```

Then, execute netcat in one terminal window:
```
ip netns exec ns2 nc -v -v -v -l -p 22
```

...and try in another window:
```
ip netns exec ns1 ssh ssh.example.com
```

The connection should open in the netcat side and you should see the SSH
version greeting.

# Using with other unmodified applications

Then execute:
```
mkdir -p /etc/netns/ns1
echo "nameserver 10.150.2.100" > /etc/netns/ns1/resolv.conf
ip link add veth0 type veth peer name veth1
ip link add veth2 type veth peer name veth3
ifconfig veth0 up
ifconfig veth1 up
ifconfig veth2 up
ifconfig veth3 up
ethtool -K veth0 rx off tx off tso off gso off gro off lro off
ethtool -K veth1 rx off tx off tso off gso off gro off lro off
ethtool -K veth2 rx off tx off tso off gso off gro off lro off
ethtool -K veth3 rx off tx off tso off gso off gro off lro off
ip netns add ns1
ip netns add ns2
ip link set veth0 netns ns1
ip link set veth3 netns ns2
ip netns exec ns1 ip addr add 10.150.2.1/24 dev veth0
ip netns exec ns2 ip addr add 10.150.1.101/24 dev veth3
ip netns exec ns1 ip link set veth0 up
ip netns exec ns2 ip link set veth3 up
ip netns exec ns1 ip link set lo up
ip netns exec ns2 ip link set lo up
ip netns exec ns2 ip route add default via 10.150.1.1
```

Then run in one terminal window and leave it running:
```
./ldpairwall/airwall/ldpairwall veth2 veth1
```

Then, execute netcat in one terminal window:
```
ip netns exec ns2 nc -v -v -v -l -p 1234
```

...and try in another window:
```
LD_PRELOAD=/path/to/cghcpcli/cghcppreload/libcghcppreload.so ip netns exec ns1 nc -v -v -v ssh.example.com 1234
```

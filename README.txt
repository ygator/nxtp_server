NXTP Raspberry Pi Server
========================

Files
-----
nxtp_server.c       C source for NXTP protocol v1 server
nxtp-server.service systemd unit
Makefile            Build, install, and uninstall targets

Prerequisites
-------------
sudo apt update
sudo apt install build-essential tzdata

Build
-----
make

Run manually
------------
./nxtp_server 12300

The server listens on all IPv4 interfaces, TCP port 12300. It is intended
for a trusted LAN. It does not implement encryption or authentication.
Each accepted connection is handled in a child process so timezone selection
(TZ/tzset) cannot race with another client. The parent reaps child processes.

Install and start the system service
------------------------------------
sudo make install

This compiles the server, creates the unprivileged 'nxtp' system account if
needed, installs the executable in /usr/local/sbin, installs the systemd unit,
then enables and starts the service.

Check service status
--------------------
sudo make status

View live logs
--------------
sudo make logs

Stop service without uninstalling
---------------------------------
sudo make disable

Start/enable service again
-------------------------
sudo make enable

Remove the service
------------------
sudo make uninstall

This stops and disables the service, removes the installed systemd unit and
server executable, and reloads systemd. It intentionally leaves the 'nxtp'
system account in place. Remove it separately, if desired, with:

sudo userdel nxtp

LAN firewall (optional but recommended)
---------------------------------------
The service binds all interfaces. Restrict access to your LAN using your
firewall/router. Example for a LAN using 192.168.1.0/24 with UFW:

sudo ufw allow from 192.168.1.0/24 to any port 12300 proto tcp

Replace 192.168.1.0/24 with your actual LAN subnet. Do not create a router
port-forward for TCP 12300 if the server is intended to remain LAN-only.

Test request bytes
------------------
The empty-timezone request bytes are: 01 00 7A
The EasternStandardTime request bytes are:
01 13 45 61 73 74 65 72 6E 53 74 61 6E 64 61 72 64 54 69 6D 65 3D

A valid response is 22 bytes: version (1), date length (10), time length (8),
date ASCII dd/MM/yyyy, time ASCII HH:mm:ss, and XOR checksum.

Notes
-----
- Empty timezone and GMT/GMTStandardTime return Europe/London, including BST.
- UTC* returns fixed UTC.
- Keep Raspberry Pi OS tzdata updated: sudo apt update && sudo apt upgrade.
- Timezone identifiers are mapped to IANA tzdata names in the C source.
  Some historical Windows timezone identifiers have no exact one-to-one IANA
  equivalent; those are mapped to the closest corresponding IANA zone.

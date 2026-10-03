Newton OS on the reMarkable Paper Pro
=====================================

A reconstruction of the Apple Newton MessagePad 2x00's operating system
(NewtonOS 2.1) as an AppLoad application, built from
https://github.com/dparnell/newton-re (docs/host-remarkable.md there).

It needs xovi and AppLoad on the tablet (AppLoad v0.6.0 or later, for the
screen to turn with the tablet and the type folio).

Install:

  scp -r newton root@10.11.99.1:/home/root/xovi/exthome/appload/
  ssh root@10.11.99.1 chmod +x /home/root/xovi/exthome/appload/newton/newton /home/root/xovi/exthome/appload/newton/run.sh

then tap AppLoad's reload button and open Newton from its launcher.  What
you do is kept in /home/root/newton-data/newton/, with a log.

The first start walks you through the Setup assistant, as a new MessagePad
does; write with the Marker, tap with a finger, type on the type folio.  The
Host page in the Newton's Preferences (Extras, Prefs, Host) has the
tablet's settings: the ink's waveform, the screen's size, touch, beaming and
docking over the network.

To remove it: rm -r /home/root/xovi/exthome/appload/newton /home/root/newton-data/newton

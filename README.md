quake3-server with added IRC client
-----------------------------------

To compile, generate a Makefile.local with the following:

BUILD_SERVER=1
BUILD_CLIENT=0
BUILD_QIRCBOT=1

You'll need the libircclient development libraries installed.

Then run make.

Running
-------

Launch as a standard quake3-server, the inbuilt IRC client will then connect to the specified IRC server/channel.  Once joined it will spawn a bot per user of the channel.  When users part/join they are removed/added to the quake3 server.


cvar options
------------

sv_irc_nick "nicknametouse"
sv_irc_channel "channeltojoin"
sv_irc_server "irc.server.org"

Make sure you copy build/release-linux-$ARCH/baseq3/vm/qagame.qvm to the right place otherwise you will be limited to ~20 bot

privmsg scores to the bot in IRC to print scores.

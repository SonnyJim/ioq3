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

If sv_irc_kick_on_kill is set to 1, the bot will try to kick users from the IRC channel when they bot in quake3 is killed.  It also sets the game mode to Team Deathmatch, so the bots don't kill each other.  Connect to the server with a quake3 client and join the red team to begin administration of the channel ;-)

cvar options
------------

sv_irc_nick 		=	IRC Bot nick
sv_irc_server		=	IRC Server to join
sv_irc_channel		=	IRC Channel to join
sv_irc_bottype		=	Default bot type to use, set to "random" for random bots
sv_irc_botskill		=	Default bot skill, set to "random" for random skilled bots	
sv_irc_enabled		=	Enable/disable IRC client
sv_irc_kick_on_kill	=	Kick users from IRC channel when bots are killled
sv_irc_kills_to_channel =	Broadcast kills to IRC channel

sv_irc_autosend_* commands operate after connecting to a server but before joining channel:
(useful for NickServ)
sv_irc_autosend_nick	=	nick to send message to
sv_irc_autosend_cmd	=	message to send
sv_irc_autosend_delay	=	delay in seconds after connecting to server to wait before sending message

sv_irc_autosend1_* commands operate after joining channel:
(useful for ChanServ)
sv_irc_autosend1_nick	=	nick to send message to
sv_irc_autosend1_cmd	=	message to send
sv_irc_autosend1_delay	=	delay in seconds after connecting to channel before sending message

Make sure you copy build/release-linux-$ARCH/baseq3/vm/qagame.qvm to the right place otherwise you will be limited to ~20 bots

privmsg scores to the bot in IRC to print scores.

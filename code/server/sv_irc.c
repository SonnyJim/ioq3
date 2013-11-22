#include "libircclient/libircclient.h"
#include "libircclient/libirc_rfcnumeric.h"
#include "stdio.h"
#include "string.h"
#include "errno.h"
#include <unistd.h>
#include <pthread.h>

#include "server.h"

#define QIRCBOT_CHANNEL "#qircbot"
#define QIRCBOT_SERVER "irc.efnet.org"
#define QIRCBOT_NICK "qircbot"
#define QIRCBOT_DEFAULTBOT "random"

#define NICKLEN 64
#define BOT_LIMIT 64

#define NUM_BOTTYPES 32
#define BOTTYPE_LEN 16
#define CTCP_PING_DELAY 60

char	*DeathNames[] = {
	"somehow",
	"by both barrels to the face",
	"in an humiliating fashion",
	"by machine gunning you down",
	"with a grenade to the face",
	"with grenade splash",
	"with a rocket to the face",
	"with rocket splash",
	"by plasmarising you to death",
	"by plasma splash",
	"by giving you a fresh hole",
	"and gave you a new hair do",
	"with a BFG",
	"with BFG splash",
	"MOD_WATER",
	"MOD_SLIME",
	"MOD_LAVA",
	"MOD_CRUSH",
	"MOD_TELEFRAG",
	"MOD_FALLING",
	"MOD_SUICIDE",
	"MOD_TARGET_LASER",
	"MOD_TRIGGER_HURT",
#ifdef MISSIONPACK
	"MOD_NAIL",
	"MOD_CHAINGUN",
	"MOD_PROXIMITY_MINE",
	"MOD_KAMIKAZE",
	"MOD_JUICED",
#endif
	"MOD_GRAPPLE"
};


enum thread_t { IRC_MONITOR, IRC_PING };

irc_session_t *session;

// List of botnames curently running
char botnicks[BOT_LIMIT][NICKLEN];
// How many bots are running
int bot_count = 0;

const char bottypes[NUM_BOTTYPES][BOTTYPE_LEN] = {"crash", "ranger", "phobos", "mynx", "orbb", "sarge", "bitterman", "grunt", \
	"hossman", "daemia", "hunter", "klesk", "angel", "wrack", "gorre", "slash", "anarki", "biker", "lucy", "patriot", "tankjr", \
		"stripe", "uriel", "razor", "keel", "visor", "bones", "cadaver", "major", "sorlag", "doom", "xaero"};
extern int botlibsetup;
extern void SV_StatusIRC (const char * nick);

static void kick_bot (const char * nick);

cvar_t *sv_irc_nick;
cvar_t *sv_irc_server;
cvar_t *sv_irc_channel;
cvar_t *sv_irc_bottype;
cvar_t *sv_irc_botskill;
cvar_t *sv_irc_enabled;
cvar_t *sv_irc_kick_on_kill;
cvar_t *sv_irc_kills_to_channel;

cvar_t *sv_irc_autosend_nick;
cvar_t *sv_irc_autosend_cmd;
cvar_t *sv_irc_autosend_delay;

cvar_t *sv_irc_autosend1_nick;
cvar_t *sv_irc_autosend1_cmd;
cvar_t *sv_irc_autosend1_delay;

int old_killer, old_killee, old_kill_method;

void irc_kill_event (int killer, int killee, int kill_method)
{
	char reason_buff[255];

	//Don't print suicides/world kills
	if (killer > 64)
		return;

	if (old_killer == killer && old_killee == killee && old_kill_method == kill_method)
	{
		Com_Printf ("Duplicate kill, ignoring\n");
		return;
	}

	old_killer = killer;
	old_killee = killee;
	old_kill_method = kill_method;

	client_t *cl, *cl1;
	cl = svs.clients;
	cl1 = svs.clients;
	cl += killer;
	cl1 += killee;
	
	
	if (sv_irc_kills_to_channel->integer)
	{
		sprintf (reason_buff, "%s killed %s %s\n", cl->name, cl1->name, DeathNames[kill_method]); 
		irc_cmd_msg (session, sv_irc_channel->string, reason_buff);
	}
	
	if (strcmp (cl1->name, sv_irc_nick->string) == 0)
	{
		Com_Printf ("Cowardly refusing to kill admin bot %s\n", sv_irc_nick->string);
		return;
	}

	Com_Printf ("IRC %s killed %s\n", cl->name, cl1->name);

	if (kill_method < 0 || kill_method >= ARRAY_LEN(DeathNames))
		sprintf (reason_buff, "%s killed you", cl->name);
	else
		sprintf (reason_buff, "%s killed you %s", cl->name, DeathNames[kill_method]);
	
	if (sv_irc_kick_on_kill->integer)
	{
		if (strncmp (cl1->name, "@", 1) == 0)
		{	
			//Trying to kick op

		}
		if (irc_cmd_kick (session, cl1->name, sv_irc_channel->string, reason_buff))
		{
			Com_Printf ("Kicked %s for being dead\n", cl1->name);
		}
		else
			Com_Printf ("IRC Kick failed, does qircbot have ops?\n");
		kick_bot (cl1->name);
	}
}

void irc_send_chat (const char * text, char * nick)
{
	char irc_msg[1024];

	strcpy (irc_msg, "");
	strcat (irc_msg, nick);
	strcat (irc_msg, " said: ");
	memcpy (irc_msg + strlen(irc_msg), text + 4, strlen (text) - 4);
	Com_Printf ("Sending Q3 Chat message to IRC %s\n", text);
	Com_Printf ("Formatted as %s\n", irc_msg);
	irc_cmd_msg (session, sv_irc_channel->string, irc_msg);
}

void irc_send_to_nick (const char * text, const char * nick)
{
	irc_cmd_msg (session, nick, text);
}

static void kick_bot (const char * nick)
{
	int i;
	char kickbot_buff[255];
	char op_buff[255];

	Com_Printf ("kick bot looking for nick %s\n", nick);

	sprintf (op_buff, "@%s", nick);

	for (i = 0; i < BOT_LIMIT; i++)
	{
		if (strcmp (botnicks[i], nick) == 0)
		{
			Com_Printf ("Kicking bot %s from %i\n", botnicks[i], i);
			sprintf (kickbot_buff, "kick %s\n", botnicks[i]);
			Com_Printf ("Kickbot string %s\n", kickbot_buff);
			Cbuf_ExecuteText (EXEC_APPEND, kickbot_buff);
			strcpy (botnicks[i], "");
			return;
		}
		if (strcmp (botnicks[i], op_buff) == 0)
		{
			//Nick was added as a channel op
			Com_Printf ("Found %s as Op %s\n", nick, botnicks[i]);
			sprintf (kickbot_buff, "kick %s\n", op_buff);
			Cbuf_ExecuteText (EXEC_APPEND, kickbot_buff);
			strcpy (botnicks[i], "");
			return;
		}
	}

	Com_Printf ("kick bot nick %s not found!\n", nick);


}

static void add_bot (const char *nick)
{
	char addbot_buff[255];
	int i;


	if (bot_count > BOT_LIMIT)
	{
		Com_Printf ("Bot limit %i reached\n", BOT_LIMIT);
		return;
	}

	//Check to see if nick is already running as a bot
	for (i = 0; i < BOT_LIMIT; i++)
	{
		if (strcmp (botnicks[i], nick) == 0)
		{
			Com_Printf ("%s already exists, ignoring\n", botnicks[i]);
			return;
		}
		sprintf (addbot_buff, "@%s", nick);
		if (strcmp (botnicks[i], addbot_buff) == 0)
		{
			Com_Printf ("%s already exists as %s, ignoring\n", botnicks[i], addbot_buff);
			return;
		}

		if (strncmp (nick, "@", 1) == 0)
		{
			strcpy (addbot_buff, "");
			strcpy (addbot_buff, nick);
			memmove (addbot_buff, addbot_buff + 1, strlen (addbot_buff + 1));
			if (strcmp (nick, addbot_buff) == 0)
			{
				Com_Printf ("Found %s deopped as %s, ignoring\n", nick, addbot_buff);
				return;
			}
		}
	}

	sprintf (addbot_buff, "addbot");

	//Add bot type
	//If Op, make them badass
	if (strncmp (nick, "@", 1) == 0 || strcmp (nick, sv_irc_nick->string) == 0)
	{
		sprintf (addbot_buff, "%s xaero 5", addbot_buff);
	}
	else
	{
		if (strcmp (sv_irc_bottype->string, "random") == 0)
		{
			i = random () * NUM_BOTTYPES;
			Com_Printf ("Picking random bot %s\n", bottypes[i]);
			sprintf (addbot_buff, "%s %s", addbot_buff, bottypes[i]);
		}
		else
			Q_strcat (addbot_buff, sizeof(addbot_buff), sv_irc_bottype->string);
		
		if (strcmp (sv_irc_botskill->string, "random") == 0)
		{
			i = random () * 5;
			Com_Printf ("Picking random skill %i\n", i);
			sprintf (addbot_buff, "%s %i", addbot_buff, i);
		}
		else
			sprintf (addbot_buff, "%s %i", addbot_buff, sv_irc_botskill->integer);
	}
	// Add team/delay/name
	sprintf (addbot_buff, "%s blue 0 %s\n", addbot_buff, nick);
	
	Com_Printf ("Executing: %s\n", addbot_buff);
	Cbuf_ExecuteText (EXEC_APPEND, addbot_buff);

	// Find a free bot slot
	for (i = 0; i < BOT_LIMIT; i++)
	{
		if (strcmp (botnicks[i], "") == 0)
		{
			// Add the bot to botnicks
			strcpy (botnicks[i], nick);
			Com_Printf ("Adding botnick %i %s\n", i, botnicks[i]);
			bot_count++;
			break;
		}
	}
}

// Add all nicks from the channel
static void channel_join_nicks (const char * nicklist)
{
	int i;
	int ircnick_found;
	char *nick, *cp;
	
	cp = strdup (nicklist);
	nick = strtok (cp, " ");

	while (nick)
	{
		Com_Printf ("Channel Join, adding %s\n", nick);
		add_bot (nick);
		//Don't spawn bots too quickly
		sleep (1);
		nick = strtok (NULL, " ");
	}

	//Double check to make sure we don't have any extra bots

	for (i = 0; i < BOT_LIMIT; i++)
	{
		ircnick_found = 0;
		if (strcmp (botnicks[i], "") != 0)
		{
			cp = strdup (nicklist);
			nick = strtok (cp, " ");
			while (nick)
			{
				if (strcmp (botnicks[i], nick) == 0)
				{
					ircnick_found = 1;
					break;
				}
				nick = strtok (NULL, " ");
			}
			
			if (ircnick_found == 0)
			{
				Com_Printf ("Channel Join found extra bot %s, removing\n", botnicks[i]);
				kick_bot (botnicks[i]);
			}
		}
	}
}

int check_autosend_cvars (void)
{
	if (strcmp (sv_irc_autosend_cmd->string, "") == 0 &&
			strcmp (sv_irc_autosend_nick->string, "") > 0)
	{
		Com_Printf ("Error! sv_autosend_nick specified but not sv_autosend_cmd\n");
		return 0;
	}
	else if (strcmp (sv_irc_autosend_cmd->string, "") > 0 &&
			strcmp (sv_irc_autosend_nick->string, "") == 0)
	{
		Com_Printf ("Error! sv_autosend_cmd specified but not sv_autosend_nick\n");
		return 0;
	}
	else if (strcmp (sv_irc_autosend_cmd->string, "") == 0 &&
			strcmp (sv_irc_autosend_nick->string, "") == 0)
		return 0;
	else
		return 1;
}

int check_autosend1_cvars (void)
{
	if (strcmp (sv_irc_autosend1_cmd->string, "") == 0 &&
			strcmp (sv_irc_autosend1_nick->string, "") > 0)
	{
		Com_Printf ("Error! sv_autosend1_nick specified but not sv_autosend1_cmd\n");
		return 0;
	}
	else if (strcmp (sv_irc_autosend1_cmd->string, "") > 0 &&
			strcmp (sv_irc_autosend1_nick->string, "") == 0)
	{
		Com_Printf ("Error! sv_autosend1_cmd specified but not sv_autosend1_nick\n");
		return 0;
	}
	else if (strcmp (sv_irc_autosend1_cmd->string, "") == 0 &&
			strcmp (sv_irc_autosend1_nick->string, "") == 0)
		return 0;
	else
		return 1;
}

//Called when connected to a server
void event_connect (irc_session_t * session, const char * event, const char * origin, const char ** params, unsigned int count)
{
	char channel[16];

	Com_Printf ("Connected to IRC server\n");

	if (check_autosend_cvars ())
	{
		if (sv_irc_autosend_delay->integer)
		{
			Com_Printf ("Waiting %i seconds before sending sv_autosend_cmd\n", sv_irc_autosend_delay->integer);
			sleep (sv_irc_autosend_delay->integer);
		}
		Com_Printf ("Sending sv_autosend_cmd to %s\n", sv_irc_autosend_nick->string);
		irc_cmd_msg (session, sv_irc_autosend_nick->string, sv_irc_autosend_cmd->string);
	
		if (sv_irc_autosend1_delay->integer)
		{
			Com_Printf ("Waiting %i seconds before sending sv_autosend_cmd\n", sv_irc_autosend_delay->integer);
			sleep (sv_irc_autosend1_delay->integer);
		}

		if (strcmp (sv_irc_autosend1_cmd->string, "") > 0)
		{
			Com_Printf ("Sending sv_autosend_cmd to %s\n", sv_irc_autosend_nick->string);
			irc_cmd_msg (session, sv_irc_autosend_nick->string, sv_irc_autosend_cmd->string);
		}
	}

	//Sleep until q3 server is ready to add bots FIXME
	while (!botlibsetup)
	{
		Com_Printf ("Waiting for BotLib to be ready\n");
		sleep (1);
	}

	Q_strncpyz(channel, sv_irc_channel->string, sizeof(channel));
	if (irc_cmd_join (session, channel, 0))
	{

		Com_Printf ("Error joining channel\n");
	}
	else
		Com_Printf ("Joined %s\n", channel);

	if (check_autosend1_cvars ())
	{
		if (sv_irc_autosend1_delay->integer)
		{
			Com_Printf ("Waiting %i seconds before sending sv_autosend_cmd\n", sv_irc_autosend1_delay->integer);
			sleep (sv_irc_autosend1_delay->integer);
		}

		Com_Printf ("Sending sv_autosend_cmd to %s\n", sv_irc_autosend1_nick->string);
		irc_cmd_msg (session, sv_irc_autosend1_nick->string, sv_irc_autosend1_cmd->string);
	}
}

void event_nick (irc_session_t * session, const char * event, const char * origin, const char ** params, unsigned int count)
{
	char nickbuf[16];
	if ( !origin || count != 1 )
		return;

	irc_target_get_nick (origin, nickbuf, sizeof(nickbuf));

	Com_Printf ("%s has changed its nick to %s\n", nickbuf, params[0]);

	//Kick and rejoin the bot
	kick_bot (origin);
	add_bot (params[0]);
}

void event_privmsg (irc_session_t * session, const char * event, const char * origin, const char ** params, unsigned int count)
{
	Com_Printf ("'%s' said to qircbot (%s): %s\n", origin ? origin : "someone", params[0], params[1] );
	if (strncmp (params[1], "scores", strlen("scores")) == 0)
	{
		Com_Printf ("Sending scores to %s\n", origin);
		SV_StatusIRC (origin);
	}
}

void event_join (irc_session_t * session, const char * event, const char * origin, const char ** params, unsigned int count)
{
	if (strcmp (origin, sv_irc_nick->string) == 0)
		Com_Printf ("Ignoring self joining channel\n");
	else
		add_bot (origin);
}

void event_part (irc_session_t * session, const char * event, const char * origin, const char ** params, unsigned int count)
{
	kick_bot (origin);
}

void event_kick (irc_session_t * session, const char * event, const char * origin, const char ** params, unsigned int count)
{
	kick_bot (params[1]);
}

void event_ctcp_action (irc_session_t * session, const char * event, const char * origin, const char ** params, unsigned int count)
{
	char serversay_string[1024];
	
	sprintf (serversay_string, "say %s %s\n", origin, params[1]);
	Com_Printf ("From %s: %s\n", sv_irc_channel->string, serversay_string);
	Cbuf_ExecuteText (EXEC_APPEND, serversay_string);
}

void event_ctcp_rep (irc_session_t * session, const char * event, const char * origin, const char ** params, unsigned int count)
{
	Com_Printf ("Ping response from %s with %s\n", origin, params[0]);
}

void event_numeric (irc_session_t * session, unsigned int event, const char * origin, const char ** params, unsigned int count)
{
	switch (event)
	{
		case LIBIRC_RFC_RPL_NAMREPLY:
		      Com_Printf ("IRC User list: %s\n", params[3]);
		      channel_join_nicks (params[3]);
		      break;
		case LIBIRC_RFC_RPL_WELCOME:
		      Com_Printf ("IRC %s\n", params[1]);
		      break;
		case LIBIRC_RFC_RPL_YOURHOST:
		      Com_Printf ("IRC %s\n", params[1]);
		case LIBIRC_RFC_RPL_ENDOFNAMES:
		      Com_Printf ("IRC End of user list\n");
		      break;
		case LIBIRC_RFC_RPL_MOTD:
		      Com_Printf ("%s\n", params[1]);
		      break;
		default:
		      Com_Printf ("IRC event_numeric: %u\n", event);
		      break;
	}
}

void event_channel (irc_session_t * session, const char * event, const char * origin, const char ** params, unsigned int count)
{
	char serversay_string[255];
	sprintf (serversay_string, "say %s: %s\n", origin, params[1]);
	Com_Printf ("From %s: %s\n", sv_irc_channel->string, serversay_string);
	Cbuf_ExecuteText (EXEC_APPEND, serversay_string);
}

int irc_connect_to_server (void)
{
	//Disconnect just to be sure
	irc_disconnect (session);
	irc_option_set (session, LIBIRC_OPTION_STRIPNICKS);
	if (irc_connect (session, sv_irc_server->string, 6667, 0, sv_irc_nick->string, "qirc", "qirc"))
	{
		Com_Printf ("Error connecting\n");
		return 0;
		//There was an error
	}
	Com_Printf ("Connected to %s\n", sv_irc_server->string);
	return 1;
}

//Thread to periodically ping the server to check it's still alive and to trigger reconnects
void *irc_ping (void *threadid)
{
	while (1)
	{
		if (irc_is_connected (session))
		{
			irc_cmd_ctcp_request (session, sv_irc_nick->string, "PING");
		}
		sleep (CTCP_PING_DELAY);
	}
}
void *irc_connection_monitor (void *threadid)
{
	int connection_attempts = 0;

	while (1)
	{
		sleep (30);
		if (!irc_is_connected (session))
		{
			connection_attempts++;
			Com_Printf ("IRC Connection monitor: %i\n", connection_attempts);
			if (connection_attempts > 4)
			{
				Com_Printf ("Restarting IRC connection");
				connection_attempts = 0;
				irc_disconnect (session);
			}
		}
	}

}

void *irc_monitor (void *threadid)
{
	int reconnection_attempts = 0;

	while (1)
	{
		if (sv_irc_enabled->integer)
		{
			if (!irc_is_connected (session))
			{
				Com_Printf ("Connecting to %s\n", sv_irc_server->string);
				if (reconnection_attempts)
					Com_Printf ("Reconnect attempt %i\n", reconnection_attempts);

				if (irc_connect_to_server ())
				{
					reconnection_attempts = 0;
					irc_run (session);
					Com_Printf ("Disconnected from %s\n", sv_irc_server->string);
				}
				else
					reconnection_attempts++;
			}
		}
		else 
		{
			if (irc_is_connected (session))
			{
				Com_Printf ("Disconnecting from %s\n", sv_irc_server->string);
				irc_disconnect (session);
			}
		}

		if (reconnection_attempts > 12)
			sleep (60);
		else
			sleep (10);
	}
}

int irc_init (void)
{
	Com_Printf ("IRC Client initilising\n");
	pthread_t irc_thread[2];
	irc_callbacks_t callbacks;
	int i;
	
	old_killer = BOT_LIMIT + 1;
	old_kill_method = 255;
	old_killee = BOT_LIMIT + 1;
	// Init it
	memset (&callbacks, 0, sizeof(callbacks));

	// Setup callbacks
	callbacks.event_connect = event_connect;
	callbacks.event_numeric = event_numeric;
	callbacks.event_privmsg = event_privmsg;
	callbacks.event_join = event_join;
	callbacks.event_part = event_part;
	callbacks.event_nick = event_nick;
	callbacks.event_channel = event_channel;
	callbacks.event_kick = event_kick;
	callbacks.event_ctcp_action = event_ctcp_action;
	callbacks.event_ctcp_rep = event_ctcp_rep;
	
	// Init cvars
	sv_irc_nick  = Cvar_Get ("sv_irc_nick", QIRCBOT_NICK, CVAR_SERVERINFO);
	sv_irc_server  = Cvar_Get ("sv_irc_server", QIRCBOT_SERVER, CVAR_SERVERINFO);
	sv_irc_channel  = Cvar_Get ("sv_irc_channel", QIRCBOT_CHANNEL, CVAR_SERVERINFO);
	sv_irc_bottype = Cvar_Get ("sv_irc_bottype", QIRCBOT_DEFAULTBOT, CVAR_ARCHIVE);
	sv_irc_enabled = Cvar_Get ("sv_irc_enabled", "1", CVAR_ARCHIVE);
	sv_irc_botskill = Cvar_Get ("sv_irc_botskill", "1", CVAR_ARCHIVE);
	sv_irc_kick_on_kill = Cvar_Get ("sv_irc_kick_on_kill", "0", CVAR_ARCHIVE);
	sv_irc_kills_to_channel = Cvar_Get ("sv_irc_kills_to_channel", "0", CVAR_ARCHIVE);

	sv_irc_autosend_nick = Cvar_Get ("sv_irc_autosend_nick", "", CVAR_ARCHIVE);
	sv_irc_autosend_cmd = Cvar_Get ("sv_irc_autosend_cmd", "", CVAR_ARCHIVE);
	sv_irc_autosend_delay = Cvar_Get ("sv_irc_autosend_delay", "2", CVAR_ARCHIVE);

	sv_irc_autosend1_nick = Cvar_Get ("sv_irc_autosend1_nick", "", CVAR_ARCHIVE);
	sv_irc_autosend1_cmd = Cvar_Get ("sv_irc_autosend1_cmd", "", CVAR_ARCHIVE);
	sv_irc_autosend1_delay = Cvar_Get ("sv_irc_autosend1_delay", "2", CVAR_ARCHIVE);

	//Set to TDM if set to kick on kills
	if (sv_irc_kick_on_kill->integer)
		Cvar_Set ("g_gametype", "3");

	// Init botnicks
	
	for (i = 0; i < BOT_LIMIT; i++)
	{
		strcpy (botnicks[i], "");
	}

	// Now create the session
	session = irc_create_session(&callbacks);

	// Handle the error
	if (!session)
	{
		Com_Printf ("Error setting up session\n");
		return 1;
	}
	/*
	if (irc_run (session))
	{
		Com_Printf ("Error connecting\n");
		return 1;
	}
	*/
	// Start monitor thread
	if (pthread_create (&irc_thread[IRC_MONITOR], NULL, irc_monitor, (void  *)0))
		Com_Printf ("Create IRC monitor thread failed: %s\n", strerror(errno));
	if (pthread_create (&irc_thread[IRC_PING], NULL, irc_ping, (void  *)0))
		Com_Printf ("Create IRC Ping thread failed: %s\n", strerror(errno));

	if (pthread_create (&irc_thread[IRC_MONITOR], NULL, irc_connection_monitor, (void  *)0))
		Com_Printf ("Create IRC Connection monitor thread failed: %s\n", strerror(errno));
	return 0;
}


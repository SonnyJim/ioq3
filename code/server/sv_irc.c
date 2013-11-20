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
#define QIRCBOT_DEFAULTBOT "crash"

#define NICKLEN 64
#define BOT_LIMIT 64

#define CTCP_PING_DELAY 60

enum thread_t { IRC_MONITOR, IRC_PING };

irc_session_t *session;

// List of botnames curently running
char botnicks[BOT_LIMIT][NICKLEN];
// How many bots are running
int bot_count = 0;

extern int botlibsetup;
extern void SV_StatusIRC (const char * nick);

cvar_t *sv_irc_nick;
cvar_t *sv_irc_server;
cvar_t *sv_irc_channel;
cvar_t *sv_irc_bottype;
cvar_t *sv_irc_enabled;

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
	char kickbot_string[64];

	Com_Printf ("kick bot looking for nick %s\n", nick);

	for (i = 0; i < BOT_LIMIT; i++)
	{
		if (strcmp (botnicks[i], nick) == 0)
		{
			Com_Printf ("Kicking bot %s from %i\n", botnicks[i], i);
			strcpy (botnicks[i], "");
			sprintf (kickbot_string, "kick %s\n", nick);
			Cbuf_ExecuteText (EXEC_APPEND, kickbot_string);
			return;
		}
	}

	Com_Printf ("kick bot nick %s not found!\n", nick);


}

static void add_bot (const char *nick)
{
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
	}

	char addbot_buff[128];
	strcpy (addbot_buff, "");
	Q_strcat (addbot_buff, sizeof(addbot_buff), "addbot ");
	
	//Add bot type
	//If Op, make them badass
	if (strncmp (nick, "@", 1) == 0)
	{
		Q_strcat (addbot_buff, sizeof(addbot_buff), "xaero");
		Q_strcat (addbot_buff, sizeof(addbot_buff), " 5 ");
	}
	else
	{
		Q_strcat (addbot_buff, sizeof(addbot_buff), sv_irc_bottype->string);
		Q_strcat (addbot_buff, sizeof(addbot_buff), " 1 ");
	}
	// Add team/delay
	Q_strcat (addbot_buff, sizeof(addbot_buff), "blue 0 ");
	// Add the name
	Q_strcat (addbot_buff, sizeof(addbot_buff), nick);
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
}

//Called when connected to a server
void event_connect (irc_session_t * session, const char * event, const char * origin, const char ** params, unsigned int count)
{
	char channel[16];

	Com_Printf ("Connected to IRC server\n");
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
	
}

void event_nick (irc_session_t * session, const char * event, const char * origin, const char ** params, unsigned int count)
{
	char nickbuf[16];
	if ( !origin || count != 1 )
		return;

	irc_target_get_nick (origin, nickbuf, sizeof(nickbuf));

	Com_Printf ("%s has changed its nick to %s\n", nickbuf, params[0]);

	//Kick and rejoin the bot
	kick_bot (nickbuf);
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
	//FIXME
	
	if (strcmp (origin, sv_irc_nick->string) == 0)
		Com_Printf ("Ignoring qircbot joining channel\n");
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
	char serversay_string[255];
	
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
	if (event == LIBIRC_RFC_RPL_NAMREPLY)
	{
		Com_Printf ("User list: %s\n", params[3]);
		channel_join_nicks (params[3]);
	}
	else if (event == LIBIRC_RFC_RPL_ENDOFNAMES)
		Com_Printf ("End of nick list\n");
	else
		Com_Printf ("event_numeric: %u\n", event);
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
	sv_irc_nick  = Cvar_Get ("sv_irc_nick", QIRCBOT_NICK, CVAR_ARCHIVE);
	sv_irc_server  = Cvar_Get ("sv_irc_server", QIRCBOT_SERVER, CVAR_ARCHIVE);
	sv_irc_channel  = Cvar_Get ("sv_irc_channel", QIRCBOT_CHANNEL, CVAR_ARCHIVE);
	sv_irc_bottype = Cvar_Get ("sv_irc_bottype", QIRCBOT_DEFAULTBOT, CVAR_ARCHIVE);
	sv_irc_enabled = Cvar_Get ("sv_irc_enabled", "1", CVAR_ARCHIVE);
	
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
	return 0;
}


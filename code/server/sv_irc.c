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
#define MAXNICKS 64
#define BOT_LIMIT 64
irc_session_t *session;

char botnick[NICKLEN], server[255];
char botnicks[MAXNICKS][NICKLEN];
int num_nicks = 0;

extern int botlibsetup;
extern void SV_StatusIRC (const char * nick);

cvar_t *sv_irc_nick;
cvar_t *sv_irc_server;
cvar_t *sv_irc_channel;
cvar_t *sv_irc_bottype;


#define CREATE_THREAD(id,func,param) (pthread_create (id, 0, func, (void *) param) != 0)
#define THREAD_FUNCTION(funcname) static void * funcname (void * arg)
#define thread_id_t pthread_t
#define _GNU_SOURCE

THREAD_FUNCTION(irc_listen)
{
	irc_session_t * sp = (irc_session_t *) arg;
	irc_run(sp);
	return 0;
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
	char kickbot_string[64] = "kick ";
	Com_Printf ("%s left channel\n", nick);
	
	Q_strcat (kickbot_string, sizeof(kickbot_string), nick);
	Cbuf_ExecuteText (EXEC_APPEND, kickbot_string);
}

static void add_bot (const char *nick)
{
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
}

// Add all nicks from the channel
static void channel_join_nicks (const char * nicklist)
{
	char *nick, *cp;
	
	cp = strdup (nicklist);
	nick = strtok (cp, " ");
	int nickpos = 0;
	
	while (nick)
	{
		if (num_nicks > BOT_LIMIT)
			break;
		Com_Printf ("Adding %s position %d\n", nick, nickpos);
		add_bot (nick);
		strcpy (botnicks[nickpos], nick); 
		//Don't spawn bots too quickly
		sleep (1);
		nick = strtok (NULL, " ");
		nickpos++;
		num_nicks++;
	}
	Com_Printf ("num_nicks %d\n", num_nicks);
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
	char serversay_string[255] = "say ";

	strcat (serversay_string, origin);
	strcat (serversay_string, " ");
	strcat (serversay_string, params[1]);
	//Com_Printf ("%s\n", serversay_string);
	Cbuf_ExecuteText (EXEC_APPEND, serversay_string);
}

void event_numeric (irc_session_t * session, unsigned int event, const char * origin, const char ** params, unsigned int count)
{
	if (event == LIBIRC_RFC_RPL_NAMREPLY)
	{
		Com_Printf ("User list: %s\n", params[3]);
		channel_join_nicks (params[3]);
	}

	if (event == LIBIRC_RFC_RPL_ENDOFNAMES)
		Com_Printf ("End of nick list\n");
}

void event_channel (irc_session_t * session, const char * event, const char * origin, const char ** params, unsigned int count)
{
	char serversay_string[255] = "say ";

	strcat (serversay_string, origin);
	strcat (serversay_string, ": ");
	strcat (serversay_string, params[1]);
	//Com_Printf ("%s\n", serversay_string);
	Cbuf_ExecuteText (EXEC_APPEND, serversay_string);
}

int irc_init (void)
{

	Com_Printf ("IRC Client initilising\n");
	// The IRC callbacks structure
	irc_callbacks_t callbacks;

	// Init it
	memset (&callbacks, 0, sizeof(callbacks));

	callbacks.event_connect = event_connect;
	callbacks.event_numeric = event_numeric;
	callbacks.event_privmsg = event_privmsg;
	callbacks.event_join = event_join;
	callbacks.event_part = event_part;
	callbacks.event_nick = event_nick;
	callbacks.event_channel = event_channel;
	callbacks.event_kick = event_kick;
	callbacks.event_ctcp_action = event_ctcp_action;

	sv_irc_nick  = Cvar_Get ("sv_irc_nick", QIRCBOT_NICK, CVAR_ARCHIVE);
	sv_irc_server  = Cvar_Get ("sv_irc_server", QIRCBOT_SERVER, CVAR_ARCHIVE);
	sv_irc_channel  = Cvar_Get ("sv_irc_channel", QIRCBOT_CHANNEL, CVAR_ARCHIVE);
	sv_irc_bottype = Cvar_Get ("sv_irc_bottype", QIRCBOT_DEFAULTBOT, CVAR_ARCHIVE);

	Q_strncpyz(botnick, sv_irc_nick->string, sizeof(botnick));
	Q_strncpyz(server, sv_irc_server->string, sizeof(server));

	// Now create the session
	session = irc_create_session(&callbacks);

	// Handle the error
	if (!session)
	{
		Com_Printf ("Error setting up session\n");
		return 1;
	}
	irc_option_set (session, LIBIRC_OPTION_STRIPNICKS);
	Com_Printf ("Connecting to %s\n", server);
	if (irc_connect (session, server, 6667, 0, botnick, "qirc", "qirc"))
	{
		Com_Printf ("Error connecting\n");
		return 1;
		//There was an error
	}
	/*
	if (irc_run (session))
	{
		Com_Printf ("Error connecting\n");
		return 1;
	}
	*/
 	thread_id_t tid;
	 if ( CREATE_THREAD (&tid, irc_listen, session) )
	 {
		 Com_Printf ("CREATE_THREAD failed: %s\n", strerror(errno));
		 return 42;
	 }
	//Probably never get here, hopefully
	return 0;
}


#include "libircclient/libircclient.h"
#include "libircclient/libirc_rfcnumeric.h"
#include "stdio.h"
#include "string.h"
#include "errno.h"
#include <unistd.h>
#include <pthread.h>

#include "server.h"

#ifndef QIRCBOT_CHANNEL
#define QIRCBOT_CHANNEL "#qircbot"
#endif

#ifndef QIRCBOT_SERVER
#define QIRCBOT_SERVER "irc.efnet.org"
#endif

#ifndef QIRCBOT_NICK
#define QIRCBOT_NICK "qircbot"
#endif

#define NICKLEN 16
#define MAXNICKS 255

irc_session_t *session;

char botnicks[MAXNICKS][NICKLEN];
int num_nicks = 0;

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

void strip_nicks (char *nicklist)
{
	char * nick = NULL;
	nick = strtok (nicklist, " ");
	int nickpos = 0;
	int i;

	while (nick)
	{
		Com_Printf ("Nick %s position %d\n", nick, nickpos);
		strcpy (botnicks[nickpos], nick); 
		nick = strtok (NULL, " ");
		nickpos++;
		num_nicks++;
	}
	Com_Printf ("num_nicks %d\n", num_nicks);
	
	for (i = 0; i < num_nicks; i++)
	{
		Com_Printf ("botnick %d %s\n", i, botnicks[i]);
	}
}


//Connected to a server
void event_connect (irc_session_t * session, const char * event, const char * origin, const char ** params, unsigned int count)
{
	Com_Printf ("Connected to %s\n", QIRCBOT_SERVER);
	if (irc_cmd_join (session, QIRCBOT_CHANNEL, 0))
	{
		Com_Printf ("Error joining channel\n");
	}
	else
		Com_Printf ("Joined %s\n", QIRCBOT_CHANNEL);
	//Get channel names
	//redundant, we get a list on join anyway.
	/*
	if (irc_cmd_names (session, "#test"))
	{
		Com_Printf ("Error fetching names");
	}
*/
	//irc_cmd_msg (session, "Sonny_Jim", "I'm awake");
}

static void kick_nick (const char * nick)
{
	char kickbot_string[64] = "kick ";
	Com_Printf ("%s left channel\n", nick);
	
	strcat (kickbot_string, nick);
	Cmd_ExecuteString (kickbot_string);
}

static void add_nick (const char * nick)
{
	char addbot_string[64] = "addbot grunt 1 blue 0 ";
	Com_Printf ("%s joined channel\n", nick);
	
	strcat (addbot_string, nick);
	Cmd_ExecuteString (addbot_string);
}

void event_nick (irc_session_t * session, const char * event, const char * origin, const char ** params, unsigned int count)
{
	char nickbuf[16];
	char addbot_string[255] = "addbot grunt 1 blue 0 ";

	if ( !origin || count != 1 )
		return;

	irc_target_get_nick (origin, nickbuf, sizeof(nickbuf));

	Com_Printf ("%s has changed its nick to %s\n", nickbuf, params[0]);

	//Kick and rejoin the bot
	kick_nick (nickbuf);
	strcat (addbot_string, params[0]);
	Cmd_ExecuteString (addbot_string);
}

void event_privmsg (irc_session_t * session, const char * event, const char * origin, const char ** params, unsigned int count)
{
	Com_Printf ("'%s' said to me (%s): %s\n", origin ? origin : "someone", params[0], params[1] );
}

void event_join (irc_session_t * session, const char * event, const char * origin, const char ** params, unsigned int count)
{
	add_nick (origin);
}

void event_part (irc_session_t * session, const char * event, const char * origin, const char ** params, unsigned int count)
{
	kick_nick (origin);
}

void event_kick (irc_session_t * session, const char * event, const char * origin, const char ** params, unsigned int count)
{
	        kick_nick (params[1]);
}

void event_ctcp_action (irc_session_t * session, const char * event, const char * origin, const char ** params, unsigned int count)
{
	char serversay_string[255] = "say ";

	strcat (serversay_string, origin);
	strcat (serversay_string, " ");
	strcat (serversay_string, params[1]);
	Com_Printf ("%s\n", serversay_string);
	Cmd_ExecuteString (serversay_string);
}

void event_numeric (irc_session_t * session, unsigned int event, const char * origin, const char ** params, unsigned int count)
{
	if (event == LIBIRC_RFC_RPL_NAMREPLY)
	{
		Com_Printf ("User list: %s\n", params[3]);
		strip_nicks (params[3]);
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
	Com_Printf ("%s\n", serversay_string);
	Cmd_ExecuteString (serversay_string);
}

int irc_init (void)
{
	Com_Printf ("IRC Client init\n");
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

	// Set up the rest of events

	// Now create the session
	session = irc_create_session(&callbacks);

	// Handle the error
	if (!session)
	{
		Com_Printf ("Error setting up session\n");
		return 1;
	}
	irc_option_set (session, LIBIRC_OPTION_STRIPNICKS);
	Com_Printf ("Connecting to %s\n", QIRCBOT_SERVER);
	if (irc_connect (session, QIRCBOT_SERVER, 6667, 0, QIRCBOT_NICK, "QIRCBOT", "QIRCBOT"))
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

